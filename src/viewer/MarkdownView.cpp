// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/MarkdownView.h"

#include "core/FontManager.h"
#include "core/ImageFetcher.h"
#include "core/ImageResolver.h"
#include "viewer/CheckboxObjectHandler.h"
#include "viewer/ChipObjectHandler.h"
#include "viewer/CodeHighlighter.h"
#include "viewer/GifObjectHandler.h"
#include "viewer/MathObjectHandler.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <functional>

namespace
{
// Coalesces bursts of edits into a single render; small enough to feel instant.
constexpr int kDefaultRenderIntervalMs = 40;

// A heading is scrolled to sit this far below the top edge, and the same slack
// decides which heading counts as current. Sharing one value keeps the two
// consistent: the section just scrolled to is always the one highlighted.
constexpr int kHeadingTopGap = 8;

// The text layout paints a scaled image without interpolation, so a large remote
// screenshot looks rough. Pre-scale it to the size the document draws it at and
// tag the ratio, so it is painted 1:1. A width of zero keeps the natural size.
QImage fitToWidth(const QImage &image, int logicalWidth, qreal ratio)
{
    if (image.isNull() || logicalWidth <= 0)
        return image;
    const qreal imageRatio = image.devicePixelRatio();
    const qreal logical = image.width() / (imageRatio > 0 ? imageRatio : 1.0);
    if (qAbs(logical - logicalWidth) < 1.0)
        return image;
    QImage scaled = image.scaledToWidth(qRound(logicalWidth * ratio), Qt::SmoothTransformation);
    if (scaled.isNull())
        return image;
    scaled.setDevicePixelRatio(ratio);
    return scaled;
}

/*
 * The document the preview renders into. Its one job is keeping decoded images
 * across rebuilds: QTextDocument throws its resource cache away whenever the
 * builder clears the text, so every render would re-read and re-decode every
 * PNG the document shows. The cache lives outside the document — the view owns
 * one hash that every render's document borrows — because each render builds
 * into a freshly created document.
 */
class PreviewDocument final : public QTextDocument
{
public:
    PreviewDocument(QObject *parent, QHash<QUrl, QVariant> *sharedImageCache, ImageFetcher *fetcher)
        : QTextDocument(parent)
        , m_imageCache(sharedImageCache)
        , m_fetcher(fetcher)
    {
        setUndoRedoEnabled(false); // a rebuilt preview has no undo history
    }

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type != QTextDocument::ImageResource)
            return QTextDocument::loadResource(type, name);

        const auto cached = m_imageCache->constFind(name);
        if (cached != m_imageCache->constEnd())
            return *cached;

        if (m_fetcher && m_fetcher->isEnabled() && ImageResolver::isRemote(name.toString())) {
            // The download is asynchronous: an empty resource now, the image
            // when it arrives. The request itself is deferred to the next
            // event-loop turn so a synchronous disk-cache hit cannot re-enter
            // the document while it is being laid out.
            ImageFetcher *fetcher = m_fetcher;
            QTimer::singleShot(0, this, [fetcher, name] {
                fetcher->fetch(name);
            });
            return {};
        }

        const QVariant loaded = QTextDocument::loadResource(type, name);
        if (!loaded.isNull())
            m_imageCache->insert(name, loaded);
        return loaded;
    }

private:
    QHash<QUrl, QVariant> *m_imageCache = nullptr;
    ImageFetcher *m_fetcher = nullptr;
};
} // namespace

MarkdownView::MarkdownView(QWidget *parent)
    : QTextBrowser(parent)
    , m_documentOwner(new QObject(this))
    , m_imageFetcher(new ImageFetcher(this))
    , m_gifHandler(new GifObjectHandler(nullptr, this))
    , m_checkboxHandler(new CheckboxObjectHandler(this))
    , m_chipHandler(new ChipObjectHandler(this))
    , m_mathHandler(new MathObjectHandler(this))
    , m_codeHighlighter(new CodeHighlighter)
{
    setOpenExternalLinks(false);
    setOpenLinks(false);
    setFrameShape(QFrame::NoFrame);
    connect(this, &QTextBrowser::anchorClicked, this, &MarkdownView::handleAnchor);

    // Own documents from the start: the stock document a QTextEdit creates is
    // its child, and a child document is deleted the moment another replaces
    // it. The render documents are owned by m_documentOwner instead.
    auto *first = new PreviewDocument(m_documentOwner, &m_imageCache, m_imageFetcher);
    m_scratchDocument = new PreviewDocument(m_documentOwner, &m_imageCache, m_imageFetcher);
    setDocument(first);
    registerObjectHandlers(document());
    m_gifHandler->setDocument(first);

    connect(m_imageFetcher, &ImageFetcher::ready, this, &MarkdownView::onRemoteImageReady);
    connect(m_imageFetcher, &ImageFetcher::failed, this, &MarkdownView::onRemoteImageFailed);

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(kDefaultRenderIntervalMs);
    connect(m_renderTimer, &QTimer::timeout, this, &MarkdownView::rebuild);

    // Dragging the scrollbar is reader-driven too; wheel and scrolling keys
    // come through the overridden event handlers.
    connect(verticalScrollBar(), &QScrollBar::sliderPressed, this, &MarkdownView::userScrolled);
    connect(verticalScrollBar(), &QScrollBar::sliderMoved, this, &MarkdownView::userScrolled);

    // Hover the code boxes to reveal their copy button.
    viewport()->setMouseTracking(true);
    m_copyFeedbackTimer = new QTimer(this);
    m_copyFeedbackTimer->setSingleShot(true);
    m_copyFeedbackTimer->setInterval(900);
    connect(m_copyFeedbackTimer, &QTimer::timeout, this, [this] {
        m_copiedFrame = nullptr;
        viewport()->update();
    });

    m_style.fonts = FontManager();
    m_style.theme = ThemeDefinition::light();
    m_codeHighlighter->setThemeDefinition(m_style.theme);
}

MarkdownView::~MarkdownView() = default;

void MarkdownView::setRenderStyle(const RenderStyle &style)
{
    m_style = style;
    m_gifHandler->setMaximumWidth(style.maxImageWidth);
    m_checkboxHandler->setColors(style.theme.markdown.weakText, style.theme.markdown.weakText, style.theme.palette.color(QPalette::Base));
    m_chipHandler->setColors(style.theme.markdown.codeBackground, style.theme.markdown.tableBorder, style.theme.markdown.weakText);
    m_mathHandler->setBodyFont(style.fonts.bodyFont());
    // Formulas are typeset at the text's em, which the document measures in
    // device-independent pixels -- not the font's points.
    const QFont bodyFont = style.fonts.bodyFont();
    m_mathHandler->setEmSize(bodyFont.pixelSize() > 0 ? qreal(bodyFont.pixelSize()) : bodyFont.pointSizeF() * logicalDpiY() / 72.0);
    m_mathHandler->setColor(style.theme.palette.color(QPalette::Text));
    m_mathHandler->setDevicePixelRatio(devicePixelRatioF());
    // Remote images (their SVG rasters especially) belong to the current
    // screen ratio; a stale ratio would soften every badge.
    m_lastDevicePixelRatio = devicePixelRatioF();
    m_imageFetcher->setDevicePixelRatio(m_lastDevicePixelRatio);
    m_codeHighlighter->setThemeDefinition(style.theme);
    setPalette(style.theme.palette);
    updateContentMargins();
    renderNow();
}

void MarkdownView::resizeEvent(QResizeEvent *event)
{
    QTextBrowser::resizeEvent(event);
    // Setting the margins inside the resize handler has no effect until the
    // next layout pass, so defer it to the event loop.
    QTimer::singleShot(0, this, [this] {
        updateContentMargins();
        // A narrower pane (an open editor, a smaller window) must re-render so
        // percentage image widths shrink instead of running off the edge.
        const int column = contentColumnWidth();
        if (column != m_lastColumnWidth) {
            m_lastColumnWidth = column;
            scheduleRerender();
        }
    });

    // Moving the window to a screen with a different scale changes the ratio
    // remote images were rasterised at; drop the cache and fetch again.
    const qreal ratio = devicePixelRatioF();
    if (!qFuzzyCompare(ratio, m_lastDevicePixelRatio)) {
        m_lastDevicePixelRatio = ratio;
        m_imageFetcher->setDevicePixelRatio(ratio);
        m_imageCache.clear();
        renderNow();
    }
}

void MarkdownView::scrollContentsBy(int dx, int dy)
{
    QTextBrowser::scrollContentsBy(dx, dy);
    updateVisibleHeading();
}

void MarkdownView::updateVisibleHeading()
{
    // A programmatic jump reports its own target right after scrolling; the
    // organic read-out would only fight it (and, near the document end, get
    // it wrong -- see the bottom rule below).
    if (m_inNavigation)
        return;

    // The reading position is the top edge of the pane. Once the pane is scrolled
    // to the end of the document there is nothing left to bring up, so the last
    // visible line counts instead — otherwise clicking the final section would
    // leave the outline pointing at the head of the document.
    const QScrollBar *bar = verticalScrollBar();
    const bool atBottom = bar->maximum() > 0 && bar->value() >= bar->maximum();
    const int readingLine = atBottom ? viewport()->height() : kHeadingTopGap;

    int found = -1;
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        if (block.blockFormat().intProperty(MarkdownBuilder::kindKey) != int(MarkdownBuilder::BlockKind::Heading))
            continue;

        // A block the layout has not reached yet sits below everything that is
        // drawn, so it counts as below the fold. Asking cursorRect() would
        // force the layout of the whole prefix up to it -- for a large document
        // that was the difference between an instant render and a frozen one.
        if (block.layout()->lineCount() == 0)
            break;

        QTextCursor cursor(block);
        if (cursorRect(cursor).top() > readingLine)
            break; // this one is still below the fold
        found = block.blockNumber();
    }

    if (found == m_visibleHeading)
        return;
    m_visibleHeading = found;
    emit visibleHeadingChanged(found);
}

void MarkdownView::updateContentMargins()
{
    // Cap the text column and centre it: reserve equal margins on both sides.
    int available = width();
    if (const QScrollBar *bar = verticalScrollBar(); bar && bar->isVisible())
        available -= bar->width();
    available -= 2 * frameWidth();

    const int margin = (std::max)(0, (available - m_style.maxContentWidth) / 2);
    setViewportMargins(margin, 0, margin, 0);
}

int MarkdownView::contentColumnWidth() const
{
    int available = width();
    if (const QScrollBar *bar = verticalScrollBar(); bar && bar->isVisible())
        available -= bar->width();
    available -= 2 * frameWidth();
    return (std::min)((std::max)(available, 0), m_style.maxContentWidth);
}

const RenderStyle &MarkdownView::renderStyle() const
{
    return m_style;
}

void MarkdownView::setBaseDir(const QString &directory)
{
    if (m_style.baseDir == directory)
        return;
    m_style.baseDir = directory;
    renderNow();
}

void MarkdownView::setRemoteImagesEnabled(bool enabled)
{
    if (m_style.remoteImagesEnabled == enabled)
        return;
    m_style.remoteImagesEnabled = enabled;
    m_imageFetcher->setEnabled(enabled);
    if (!enabled)
        m_imageCache.clear(); // don't let a stale download outlive the toggle
    renderNow();
}

bool MarkdownView::remoteImagesEnabled() const
{
    return m_style.remoteImagesEnabled;
}

ImageFetcher *MarkdownView::imageFetcher() const
{
    return m_imageFetcher;
}

void MarkdownView::onRemoteImageReady(const QUrl &url, const QImage &image)
{
    if (image.isNull())
        return;
    m_imageCache.insert(url, image); // the natural image, for later widths

    // Show it in place: the resource is stored on the document and the blocks
    // that hold this image are re-laid out, so the row grows to the image's
    // size without touching the rest of the document. Pre-scale to the drawn
    // width so the paint is 1:1 and stays sharp.
    QTextDocument *doc = document();
    if (!doc)
        return;
    const qreal ratio = devicePixelRatioF();
    bool placed = false;
    for (QTextBlock block = doc->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (!format.isImageFormat())
                continue;
            if (QUrl(format.toImageFormat().name()) != url)
                continue;
            doc->addResource(QTextDocument::ImageResource, url, fitToWidth(image, int(format.toImageFormat().width()), ratio));
            doc->markContentsDirty(it.fragment().position(), it.fragment().length());
            placed = true;
        }
    }
    if (!placed)
        doc->addResource(QTextDocument::ImageResource, url, image);
}

// Give every already-fetched remote image the size this render draws it at, so
// a rebuild (a resize, a theme switch) keeps it sharp instead of re-fetching.
void MarkdownView::fitRemoteImages(QTextDocument *doc)
{
    if (!doc)
        return;
    const qreal ratio = devicePixelRatioF();
    for (QTextBlock block = doc->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (!format.isImageFormat())
                continue;
            const QUrl url(format.toImageFormat().name());
            const auto cached = m_imageCache.constFind(url);
            if (cached == m_imageCache.constEnd())
                continue;
            const QImage image = cached->value<QImage>();
            if (image.isNull())
                continue;
            doc->addResource(QTextDocument::ImageResource, url, fitToWidth(image, int(format.toImageFormat().width()), ratio));
        }
    }
}

void MarkdownView::onRemoteImageFailed(const QUrl &url)
{
    // The download will not produce an image: fall back to the alt-text chip
    // the disabled path would have shown, drawn once into the cache under the
    // image's own URL so the pending row settles instead of staying empty.
    QTextDocument *doc = document();
    if (!doc)
        return;
    for (QTextBlock block = doc->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (!format.isImageFormat())
                continue;
            const QTextImageFormat imageFormat = format.toImageFormat();
            if (QUrl(imageFormat.name()) != url)
                continue;
            const QString alt = imageFormat.property(MarkdownBuilder::imageAltKey).toString();
            if (alt.isEmpty())
                continue;
            const QImage placeholder = m_chipHandler->renderChip(alt, imageFormat.fontPointSize() > 0 ? imageFormat.fontPointSize() : 11.0);
            if (placeholder.isNull())
                continue;
            m_imageCache.insert(url, placeholder);
            doc->addResource(QTextDocument::ImageResource, url, placeholder);
            doc->markContentsDirty(it.fragment().position(), it.fragment().length());
        }
    }
}

void MarkdownView::setMarkdown(const QString &markdown)
{
    m_source = markdown;
    // A fresh edit gives up any earlier "render anyway": keeping a huge document
    // live is exactly what the size policy exists to prevent.
    m_previewRequested = false;

    const int interval = effectiveRenderInterval();
    if (interval == 0) {
        rebuild();
    } else if (!m_renderTimer->isActive()) {
        // Coalesce: the first change arms the timer, later ones are absorbed.
        // A degraded document arms it with a longer interval, so a burst of
        // keystrokes still costs one render rather than one per key.
        m_renderTimer->start(interval);
    }
}

void MarkdownView::renderNow()
{
    m_renderTimer->stop();
    rebuild();
}

void MarkdownView::scheduleRerender()
{
    if (!m_renderTimer->isActive())
        m_renderTimer->start();
}

int MarkdownView::renderInterval() const
{
    return m_renderTimer->interval();
}

void MarkdownView::setRenderInterval(int milliseconds)
{
    m_renderTimer->setInterval(milliseconds);
}

int MarkdownView::effectiveRenderInterval() const
{
    // The interval configured by the caller is the floor; a degraded document
    // may ask for a longer one, so typing costs at most one render per interval
    // instead of one per keystroke.
    if (!m_limits.degraded()) {
        const int baseInterval = m_renderTimer->interval();
        if (baseInterval == 0)
            return 0;
        // Adapt debounce to render duration so heavy documents do not starve the GUI thread.
        return (std::max)(baseInterval, (std::min)(350, int(m_lastRenderMs * 1.5)));
    }
    return (std::max)(m_renderTimer->interval(), m_limits.renderIntervalMs());
}

void MarkdownView::setDocumentLimits(const DocumentLimits &limits)
{
    const bool wasPaused = previewPaused();

    m_limits = limits;
    m_previewRequested = false;
    m_codeHighlighter->setLimits(limits.codeHighlightMaxBlockChars(), limits.codeHighlightMaxTotalChars());

    // Entering the paused state is cheap, so put the notice up at once.
    // Leaving it must not redraw what is on screen: that could be the very
    // document we are replacing. The caller's setMarkdown() renders the new one.
    if (previewPaused()) {
        if (!wasPaused)
            rebuild();
    } else {
        m_showingPlaceholder = false;
    }
}

bool MarkdownView::previewPaused() const
{
    return !m_limits.previewEnabled() && !m_previewRequested;
}

void MarkdownView::renderAnyway()
{
    m_previewRequested = true;
    m_renderTimer->stop();
    rebuild();
}

void MarkdownView::rebuild()
{
    if (previewPaused()) {
        // The placeholder is cheap, but rebuilding it on every keystroke is
        // still pointless work: once it is up, leave it alone.
        if (!m_showingPlaceholder)
            buildPlaceholder();
        return;
    }
    m_showingPlaceholder = false;

    QElapsedTimer timer;
    timer.start();

    // Read the anchor off the document that is showing right now; after the
    // swap the same text is looked for in the fresh one.
    const QString anchorText = cursorForPosition(QPoint(0, 0)).block().text();
    const int anchorIndex = cursorForPosition(QPoint(0, 0)).block().blockNumber();
    QScrollBar *scrollBar = verticalScrollBar();
    const int previousValue = scrollBar->value();

    // Build into a freshly created detached document. It belongs to no widget,
    // so no layout can react to it: a document that has already been shown
    // carries started-layout state, and building into such a document makes
    // QTextDocumentLayout relayout while the builder inserts blocks (and again
    // under every block the highlighter touches) -- building into the live
    // document was exactly that interleaving, and it is where the bulk of an
    // open-file render went. QTextEdit installs the page size at the swap, and
    // the remaining layout runs through its own bounded steps.
    m_scratchDocument->deleteLater();
    m_scratchDocument = new PreviewDocument(m_documentOwner, &m_imageCache, m_imageFetcher);

    // Resolve percentage widths against the column the pane actually has right
    // now, so a width="100%" hero fits it instead of overflowing.
    m_style.contentWidth = contentColumnWidth();
    m_lastColumnWidth = m_style.contentWidth;

    const bool built = MarkdownBuilder::build(m_scratchDocument, m_source, m_style);
    if (!built) {
        // A parse failure leaves the scratch in a partial state: keep showing
        // the previous render, exactly as the in-place build did.
        qWarning("total-markdown: markdown parse failed; leaving the previous render in place");
        m_lastRenderMs = int(timer.elapsed());
        return;
    }
    fitRemoteImages(m_scratchDocument);
    m_codeHighlighter->highlight(m_scratchDocument);

    // Register the object handlers now -- after build and highlight, but
    // before the swap. Two windows must not meet: a layout that already knows
    // the object types reacts to every insertion (building with them attached
    // relayouts under each block and typesets formulas mid-build), and
    // QTextEdit's setDocument machinery measures the document synchronously
    // for the scrollbar ranges -- an object type without a handler there
    // measures as zero width, which is how a centred badge row ended up drawn
    // over the column's right edge until the next full relayout.
    registerObjectHandlers(m_scratchDocument);

    // Swap it in. The outgoing document is reparented away from the edit (a
    // document parented to a QTextEdit is deleted when another replaces it)
    // and parked in the scratch slot, which the next rebuild replaces with a
    // fresh document and drops.
    QTextDocument *outgoing = document();
    m_hoveredFrame = nullptr;
    m_copiedFrame = nullptr;
    outgoing->setParent(m_documentOwner);
    setDocument(m_scratchDocument);
    m_scratchDocument = outgoing;

    m_gifHandler->setDocument(document());
    m_gifHandler->syncToDocument(); // stops movies for images no longer present
    applyFindSelections(); // the old selections pointed at the old document

    // Prefer to keep the anchor block visually in place; fall back to the
    // absolute scroll offset when it cannot be found (e.g. the block was
    // edited away). Among equal texts, pick the occurrence closest to the
    // anchor's original index so duplicated lines do not jump the view.
    int bestPosition = -1;
    int bestDistance = 0;
    if (!anchorText.isEmpty()) {
        for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
            if (block.text() != anchorText)
                continue;
            const int distance = qAbs(block.blockNumber() - anchorIndex);
            if (bestPosition < 0 || distance < bestDistance) {
                bestPosition = block.position();
                bestDistance = distance;
            }
        }
    }

    if (bestPosition >= 0) {
        QTextCursor cursor(document());
        cursor.setPosition(bestPosition);
        const int top = cursorRect(cursor).top();
        if (top != 0)
            scrollBar->setValue(scrollBar->value() + top);
    } else {
        scrollBar->setValue(previousValue);
    }

    m_lastRenderMs = int(timer.elapsed());
    updateVisibleHeading();
    emit rendered();
}

void MarkdownView::registerObjectHandlers(QTextDocument *document)
{
    QAbstractTextDocumentLayout *layout = document->documentLayout();
    layout->registerHandler(MarkdownBuilder::gifObjectType, m_gifHandler);
    layout->registerHandler(MarkdownBuilder::checkboxObjectType, m_checkboxHandler);
    layout->registerHandler(MarkdownBuilder::chipObjectType, m_chipHandler);
    layout->registerHandler(MarkdownBuilder::mathObjectType, m_mathHandler);
}

void MarkdownView::buildPlaceholder()
{
    QTextDocument *target = document();
    target->clear();
    m_gifHandler->syncToDocument(); // stops animations from the previous render

    const ThemeDefinition &theme = m_style.theme;

    QTextCharFormat title;
    title.setForeground(theme.markdown.heading);
    title.setFont(m_style.fonts.headingFont(3));
    title.setFontWeight(QFont::Bold);

    QTextCharFormat body;
    body.setForeground(theme.markdown.weakText);
    body.setFont(m_style.fonts.bodyFont());

    // A link, not a button: the renderer already routes anchors to the viewer,
    // so the action needs no extra widget on top of the document.
    QTextCharFormat action;
    action.setForeground(theme.markdown.link);
    action.setFont(m_style.fonts.bodyFont());
    action.setAnchor(true);
    action.setAnchorHref(QStringLiteral("mmd-action:render-preview"));

    QTextBlockFormat centred;
    centred.setAlignment(Qt::AlignHCenter);

    QTextCursor cursor(target);
    cursor.setBlockFormat(centred);
    cursor.insertText(tr("Preview paused"), title);
    cursor.insertBlock(centred);
    cursor.insertText(
        tr("This document is %1. Rendering it on every keystroke would freeze the window.").arg(QLocale().formattedDataSize(m_limits.sizeBytes())),
        body);
    cursor.insertBlock(centred);
    cursor.insertText(tr("Render anyway"), action);

    m_showingPlaceholder = true;
    m_lastRenderMs = 0;
    updateVisibleHeading();
    emit rendered();
}

void MarkdownView::setFindMatches(const QVector<FindState::Match> &matches, int currentIndex)
{
    m_findMatches = matches;
    m_findCurrent = currentIndex;
    applyFindSelections();

    if (currentIndex < 0 || currentIndex >= m_findMatches.size())
        return;

    // A read-only pane keeps no caret of its own, so the hit is brought into
    // view by scrolling: cursorRect() reports where the match ended up.
    const FindState::Match &match = m_findMatches.at(currentIndex);
    QTextCursor cursor(document());
    cursor.setPosition(match.position);
    const int top = cursorRect(cursor).top();
    verticalScrollBar()->setValue(verticalScrollBar()->value() + top - height() / 3);
}

void MarkdownView::clearFindMatches()
{
    if (m_findMatches.isEmpty() && m_findCurrent < 0)
        return;
    m_findMatches.clear();
    m_findCurrent = -1;
    applyFindSelections();
}

void MarkdownView::applyFindSelections()
{
    if (m_findMatches.isEmpty()) {
        setExtraSelections({});
        return;
    }

    QList<QTextEdit::ExtraSelection> selections;
    selections.reserve(m_findMatches.size());
    for (int i = 0; i < m_findMatches.size(); ++i) {
        const FindState::Match &match = m_findMatches.at(i);
        const bool isCurrent = i == m_findCurrent;

        QTextEdit::ExtraSelection selection;
        QColor background = palette().color(QPalette::Highlight);
        if (!isCurrent)
            background.setAlpha(90); // a hint of the same theme colour
        selection.format.setBackground(background);
        if (isCurrent)
            selection.format.setForeground(palette().color(QPalette::HighlightedText));
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(match.position);
        selection.cursor.setPosition(match.position + match.length, QTextCursor::KeepAnchor);
        selections.append(selection);
    }
    setExtraSelections(selections);
}
QVector<QTextFrame *> MarkdownView::codeFrames() const
{
    QVector<QTextFrame *> frames;
    std::function<void(QTextFrame *)> walk = [&](QTextFrame *frame) {
        for (QTextFrame *child : frame->childFrames()) {
            if (child->frameFormat().intProperty(MarkdownBuilder::kindKey) == int(MarkdownBuilder::BlockKind::Code))
                frames.append(child);
            walk(child);
        }
    };
    walk(document()->rootFrame());
    return frames;
}

QRect MarkdownView::codeFrameRect(QTextFrame *frame) const
{
    const QRectF docRect = document()->documentLayout()->frameBoundingRect(frame);
    return docRect.translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value()).toRect();
}

QRect MarkdownView::copyButtonRect(QTextFrame *frame) const
{
    constexpr int kSize = 26;
    constexpr int kInset = 6;
    const QRect frameRect = codeFrameRect(frame);
    return QRect(frameRect.right() - kInset - kSize, frameRect.top() + kInset, kSize, kSize);
}

QString MarkdownView::codeText(QTextFrame *frame) const
{
    QString text;
    for (QTextFrame::iterator it = frame->begin(); !it.atEnd(); ++it) {
        const QTextBlock block = it.currentBlock();
        if (!block.isValid())
            continue;
        if (!text.isEmpty())
            text += QLatin1Char('\n');
        text += block.text();
    }
    return text;
}

bool MarkdownView::copyCodeAt(const QPoint &viewportPos)
{
    if (!m_hoveredFrame || !copyButtonRect(m_hoveredFrame).contains(viewportPos))
        return false;

    QGuiApplication::clipboard()->setText(codeText(m_hoveredFrame));
    m_copiedFrame = m_hoveredFrame;
    m_copyFeedbackTimer->start();
    viewport()->update();
    return true;
}

void MarkdownView::updateHoveredFrame(const QPoint &pos)
{
    QTextFrame *hovered = nullptr;
    for (QTextFrame *frame : codeFrames()) {
        if (codeFrameRect(frame).contains(pos)) {
            hovered = frame;
            break;
        }
    }

    if (hovered != m_hoveredFrame) {
        m_hoveredFrame = hovered;
        viewport()->update();
    }

    const bool overButton = m_hoveredFrame && copyButtonRect(m_hoveredFrame).contains(pos);
    // A link — in text or on an image badge — asks for the hand; setting it here
    // also keeps the cursor right when the move is ours and the layout does not
    // re-run its own hover logic.
    const bool overLink = !anchorAt(pos).isEmpty();
    viewport()->setCursor(overButton || overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
}

void MarkdownView::mouseMoveEvent(QMouseEvent *event)
{
    updateHoveredFrame(event->pos());
    if (isCheckboxAt(event->pos()))
        viewport()->setCursor(Qt::PointingHandCursor);
    QTextBrowser::mouseMoveEvent(event);
}

void MarkdownView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && copyCodeAt(event->pos()))
        return;
    if (event->button() == Qt::LeftButton) {
        m_pressPosition = event->pos();
        m_hasPressPosition = true;
    }
    QTextBrowser::mousePressEvent(event);
}

void MarkdownView::mouseReleaseEvent(QMouseEvent *event)
{
    const bool hadPress = m_hasPressPosition;
    const QPoint pressedAt = m_pressPosition;
    m_hasPressPosition = false;

    // A drag is a selection, not a jump; a link click belongs to the anchor
    // handler; and the copy button already acted on the press.
    if (event->button() == Qt::LeftButton && hadPress && anchorAt(event->pos()).isEmpty()
        && (event->pos() - pressedAt).manhattanLength() <= QApplication::startDragDistance()) {
        const int line = sourceLineAt(event->pos());
        if (isCheckboxAt(event->pos()) && line >= 0) {
            emit taskCheckboxToggled(line);
        } else if (line >= 0) {
            emit sourceLineActivated(line);
        }
    }

    QTextBrowser::mouseReleaseEvent(event);
}

bool MarkdownView::isCheckboxAt(const QPoint &viewportPos) const
{
    const QTextCursor cursor = cursorForPosition(viewportPos);
    const QTextBlock block = cursor.block();
    if (!block.isValid())
        return false;

    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextCharFormat fmt = it.fragment().charFormat();
        if (fmt.objectType() == MarkdownBuilder::checkboxObjectType) {
            const int fragStart = it.fragment().position() - block.position();
            const int posInBlock = cursor.positionInBlock();
            if (posInBlock >= fragStart && posInBlock <= fragStart + 2)
                return true;
            break;
        }
    }
    return false;
}

int MarkdownView::sourceLineAt(const QPoint &viewportPos) const
{
    const QTextBlock block = cursorForPosition(viewportPos).block();
    if (!block.isValid())
        return -1;

    const QVariant line = block.blockFormat().property(MarkdownBuilder::sourceLineKey);
    return line.isValid() ? line.toInt() : -1;
}

void MarkdownView::leaveEvent(QEvent *event)
{
    if (m_hoveredFrame) {
        m_hoveredFrame = nullptr;
        viewport()->update();
    }
    // The hand cursor must not outlive the link it was hovering: the next
    // mouseMoveEvent may be far away, so restore the text cursor here.
    viewport()->setCursor(Qt::IBeamCursor);
    QTextBrowser::leaveEvent(event);
}

// These three report *reader-driven* movement: wheel, scrollbar drag, scrolling
// keys. Programmatic jumps settle their own scroll and report their target
// through visibleHeadingChanged, so a window can tell a navigation from a
// reading move and pin the outline bar across the former.
void MarkdownView::wheelEvent(QWheelEvent *event)
{
    QTextBrowser::wheelEvent(event);
    emit userScrolled();
}

void MarkdownView::keyPressEvent(QKeyEvent *event)
{
    QTextBrowser::keyPressEvent(event);
    switch (event->key()) {
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
        emit userScrolled();
        break;
    default:
        break;
    }
}

void MarkdownView::paintEvent(QPaintEvent *event)
{
    QTextBrowser::paintEvent(event);

    if (!m_hoveredFrame)
        return;

    QPainter painter(viewport());
    drawCopyButton(painter, copyButtonRect(m_hoveredFrame), m_hoveredFrame == m_copiedFrame);
}

void MarkdownView::drawCopyButton(QPainter &painter, const QRect &button, bool copied) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false); // sharp, flat corners

    const QColor ink = m_style.theme.markdown.codeText;
    constexpr float kChipAlpha = 30.0f / 255.0f;
    QColor chip = ink;
    chip.setAlpha(30);
    QColor border = ink;
    border.setAlpha(56);
    painter.setPen(QPen(border, 1));
    painter.setBrush(chip);
    painter.drawRect(button.adjusted(0, 0, -1, -1)); // sharp square, no rounding

    const QColor glyph = m_style.theme.markdown.weakText;
    if (copied) {
        // A sharp tick.
        painter.setPen(QPen(glyph, 2));
        painter.drawLine(button.left() + 8, button.top() + 13, button.left() + 11, button.top() + 16);
        painter.drawLine(button.left() + 11, button.top() + 16, button.left() + 18, button.top() + 9);
    } else {
        // Two overlapping sheets; the front one is filled to occlude the back.
        const QColor under = m_style.theme.markdown.codeBackground;
        const QColor chipOpaque = QColor::fromRgbF(under.redF() * (1 - kChipAlpha) + ink.redF() * kChipAlpha,
                                                   under.greenF() * (1 - kChipAlpha) + ink.greenF() * kChipAlpha,
                                                   under.blueF() * (1 - kChipAlpha) + ink.blueF() * kChipAlpha);
        const QRect back(button.left() + 7, button.top() + 6, 9, 10);
        const QRect front(button.left() + 11, button.top() + 10, 9, 10);
        painter.setPen(QPen(glyph, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(back);
        painter.setBrush(chipOpaque);
        painter.setPen(Qt::NoPen);
        painter.drawRect(front);
        painter.setPen(QPen(glyph, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(front);
    }

    painter.restore();
}

bool MarkdownView::scrollToBlock(int blockNumber)
{
    const QTextBlock block = document()->findBlockByNumber(blockNumber);
    if (!block.isValid())
        return false;

    QTextCursor cursor(block);
    const int top = cursorRect(cursor).top();
    // Land the heading at the top edge, with the same gap the "current section"
    // rule tolerates, so the outline marks the section it just jumped to.
    m_inNavigation = true;
    verticalScrollBar()->setValue(verticalScrollBar()->value() + top - kHeadingTopGap);
    m_inNavigation = false;

    // The jump names its own target, even when the document is too short to
    // scroll that far and the bar clamps at the bottom: what the reader asked
    // for is the section clicked, not whatever sits under the reading line.
    if (m_visibleHeading == blockNumber)
        return true;
    m_visibleHeading = blockNumber;
    emit visibleHeadingChanged(blockNumber);
    return true;
}

bool MarkdownView::scrollToAnchor(const QString &slug)
{
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        if (block.blockFormat().intProperty(MarkdownBuilder::kindKey) != int(MarkdownBuilder::BlockKind::Heading))
            continue;
        if (MarkdownBuilder::headingSlug(block.text()) != slug)
            continue;
        return scrollToBlock(block.blockNumber());
    }
    return false;
}

void MarkdownView::handleAnchor(const QUrl &url)
{
    // Our own links (the paused-preview action) never leave the application.
    if (url.scheme() == QLatin1String("mmd-action")) {
        if (url.path() == QLatin1String("render-preview"))
            renderAnyway();
        return;
    }

    // In-document anchors scroll the preview instead of navigating away (which
    // would blank the view).
    if (url.hasFragment() && (url.path().isEmpty() || url.scheme() == QLatin1String("file"))) {
        if (scrollToAnchor(url.fragment()))
            return;
    }

    if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))
        QDesktopServices::openUrl(url);
    // Other schemes (mailto, relative files) are left untouched on purpose.
}
