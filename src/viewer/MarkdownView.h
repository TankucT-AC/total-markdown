// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_VIEWER_MARKDOWNVIEW_H
#define TOTALMARKDOWN_VIEWER_MARKDOWNVIEW_H

#include "core/DocumentLimits.h"
#include "core/FindState.h"
#include "core/MarkdownBuilder.h"

#include <QHash>
#include <QTextBrowser>
#include <QUrl>

#include <memory>

class QTimer;
class GifObjectHandler;
class CodeHighlighter;
class CheckboxObjectHandler;
class ChipObjectHandler;
class MathObjectHandler;
class ImageFetcher;

/*
 * Preview pane: renders markdown source into its QTextDocument and keeps the
 * result up to date as the source changes.
 *
 * Rendering is incremental in the sense that matters for a live preview:
 * repeated setMarkdown() calls within the debounce window collapse into a
 * single rebuild, so a burst of keystrokes costs one render, not one per key.
 * The scroll position is preserved across a rebuild by anchoring on the block
 * currently at the top of the viewport.
 *
 * Each rebuild builds into a freshly created, fully detached document and
 * swaps it in atomically. Building into the document the widget is showing
 * makes QTextDocumentLayout relayout between cursor insertions, and a document
 * that has already been shown keeps that eager state even after it is swapped
 * out -- a virgin build document plus an atomic swap keeps the whole rebuild
 * off the layout machinery. The object handlers register on the fresh
 * document's layout before the swap: QTextEdit measures the document inside
 * setDocument, and an object type without a handler would measure as zero
 * width. Decoded images live in a cache owned by the view, so no render
 * re-decodes what an earlier one already showed.
 */
class MarkdownView : public QTextBrowser
{
    Q_OBJECT

public:
    explicit MarkdownView(QWidget *parent = nullptr);
    ~MarkdownView() override;

    void setRenderStyle(const RenderStyle &style);
    const RenderStyle &renderStyle() const;

    void setBaseDir(const QString &directory);

    // Turns remote image loading on or off. Off (the default until the app
    // wires its setting in) renders remote references as alt-text chips; on
    // downloads them in the background. Toggling re-renders the document.
    void setRemoteImagesEnabled(bool enabled);
    bool remoteImagesEnabled() const;

    // The network fetcher, exposed so the app can configure it (cache
    // directory, and the private-network escape hatch tests need).
    ImageFetcher *imageFetcher() const;

    // Applies the document-size policy. For a document past the preview limit
    // the pane shows a short explanation instead of the render, so editing
    // stays responsive; renderAnyway() overrides that once.
    void setDocumentLimits(const DocumentLimits &limits);

    // Renders the current source even though the limits said not to. Wired to
    // the "Render anyway" link of the paused-preview placeholder.
    void renderAnyway();

    // True while the source is too big to render without an explicit request.
    bool previewPaused() const;

    // Scrolls the heading that lives in this rendered block to the top of the
    // pane. Returns false when no such block exists.
    bool scrollToBlock(int blockNumber);

    // The heading currently at the top of the pane, as a block number, or -1
    // while above the first heading.
    int visibleHeading() const
    {
        return m_visibleHeading;
    }

    // Duration of the last rebuild in milliseconds; 0 while the preview is
    // paused. Surfaced in the status bar.
    int lastRenderMs() const
    {
        return m_lastRenderMs;
    }

    // Search decorations. The pane is read-only, so it highlights the hits and
    // scrolls the current one into view instead of moving a caret.
    void setFindMatches(const QVector<FindState::Match> &matches, int currentIndex);
    void clearFindMatches();

    // Schedules a render; calls within the interval are coalesced.
    void setMarkdown(const QString &markdown);

    // Renders immediately, bypassing the debounce.
    void renderNow();

    // Re-renders the current source through the debounce, so a burst of
    // layout changes (e.g. dragging the splitter) coalesces into one render.
    void scheduleRerender();

    int renderInterval() const;
    void setRenderInterval(int milliseconds);

    // Code boxes of the current document, in document order. Each is the
    // QTextFrame the builder wrapped a fenced code block in.
    QVector<QTextFrame *> codeFrames() const;

    // The screen (viewport) rectangle of a code box and of its copy button.
    QRect codeFrameRect(QTextFrame *frame) const;
    QRect copyButtonRect(QTextFrame *frame) const;

    // Plain text of a code box.
    QString codeText(QTextFrame *frame) const;

    // Copies the code box under the given viewport point, if the point hits its
    // copy button. Returns true when something was copied.
    bool copyCodeAt(const QPoint &viewportPos);

    // Scrolls to the heading whose GitHub anchor is `slug`. Returns false when
    // no heading matches.
    bool scrollToAnchor(const QString &slug);

    // Zero-based source line of the block under a viewport point, or -1 when the
    // point is not over a block the builder could map. Click-to-source.
    int sourceLineAt(const QPoint &viewportPos) const;
    bool isCheckboxAt(const QPoint &viewportPos) const;

signals:
    void rendered();

    // A click landed on a task-list checkbox in the preview.
    void taskCheckboxToggled(int line);

    // A plain click landed somewhere in the document: the editor should put its
    // caret on this source line. Zed-style "click the preview to edit here".
    void sourceLineActivated(int line);

    // The heading the reader is currently under, as a rendered block number, or
    // -1 while above the first heading. Drives the outline panel.
    void visibleHeadingChanged(int blockNumber);

    // The reader moved the view themselves: wheel, drag of the scrollbar, or a
    // scrolling key. Distinct from a programmatic jump, which reports its own
    // target through visibleHeadingChanged instead.
    void userScrolled();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void rebuild();
    void buildPlaceholder();
    void applyFindSelections();
    void registerObjectHandlers(QTextDocument *document);
    void updateVisibleHeading();
    int effectiveRenderInterval() const;
    void updateContentMargins();
    // The text column the pane currently shows, in pixels: the live width that
    // percentage image widths are resolved against.
    int contentColumnWidth() const;
    void updateHoveredFrame(const QPoint &pos);
    void drawCopyButton(QPainter &painter, const QRect &button, bool copied) const;
    void handleAnchor(const QUrl &url);
    void onRemoteImageReady(const QUrl &url, const QImage &image);
    void onRemoteImageFailed(const QUrl &url);
    // Pre-scale fetched remote images to the size the document draws them at.
    void fitRemoteImages(QTextDocument *document);

    RenderStyle m_style;
    DocumentLimits m_limits;
    QString m_source;
    QTimer *m_renderTimer = nullptr;
    // The two documents take turns being shown: one is on screen while the
    // other is being built. Both are parented to m_documentOwner, never to the
    // view itself — a document parented to a QTextEdit is deleted when another
    // one replaces it.
    QObject *m_documentOwner = nullptr;
    // The document slot the next rebuild fills with a fresh document; the one
    // parked there is the previously shown document, kept alive until then so
    // anything still reading it survives the swap.
    QTextDocument *m_scratchDocument = nullptr;
    // Decoded images across rebuilds; every render document borrows this one
    // hash, so a picture the reader already saw is never decoded twice.
    QHash<QUrl, QVariant> m_imageCache;
    ImageFetcher *m_imageFetcher = nullptr;
    // The screen ratio the current remote-image rasters were produced at.
    qreal m_lastDevicePixelRatio = 1.0;
    // The column width the current render was built for; a resize that changes
    // it triggers a re-render so percentage images keep fitting the pane.
    int m_lastColumnWidth = -1;
    GifObjectHandler *m_gifHandler = nullptr;
    CheckboxObjectHandler *m_checkboxHandler = nullptr;
    ChipObjectHandler *m_chipHandler = nullptr;
    MathObjectHandler *m_mathHandler = nullptr;
    std::unique_ptr<CodeHighlighter> m_codeHighlighter;

    QTextFrame *m_hoveredFrame = nullptr;
    QTextFrame *m_copiedFrame = nullptr;
    QTimer *m_copyFeedbackTimer = nullptr;
    bool m_showingPlaceholder = false;
    bool m_previewRequested = false; // the user asked for this one render
    int m_lastRenderMs = 0;
    QVector<FindState::Match> m_findMatches;
    int m_findCurrent = -1;
    int m_visibleHeading = -1;
    // True while a programmatic jump (outline click, anchor) is settling its
    // own scroll position; organic updates stay quiet until it lands.
    bool m_inNavigation = false;
    // Where the last left press landed, so a drag (a selection) does not also
    // move the editor caret.
    QPoint m_pressPosition;
    bool m_hasPressPosition = false;
};

#endif // TOTALMARKDOWN_VIEWER_MARKDOWNVIEW_H
