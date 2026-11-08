// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "TestHttpServer.h"
#include "core/DocumentLimits.h"
#include "core/ImageFetcher.h"
#include "core/MarkdownBuilder.h"
#include "core/Theme.h"
#include "viewer/GifObjectHandler.h"
#include "viewer/MarkdownView.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextImageFormat>
#include <QTextLayout>

class ViewTest : public QObject
{
    Q_OBJECT

private slots:
    void rendersMarkdownIntoDocument();
    void previewHighlightsFencedCode();
    void themeSwitchRewritesDocument();
    void burstOfChangesCoalescesIntoOneRender();
    void renderIsImmediateWhenIntervalIsZero();
    void scrollIsPreservedAcrossRerender();
    void baseDirResolvesImages();
    void contentIsCappedByMaxWidth();
    void codeFramesAreFoundAfterRender();
    void copyButtonCopiesTheCode();
    void anchorScrollsInsteadOfBlankTheView();
    void hugeDocumentPausesThePreview();
    void renderAnywayOverridesThePause();
    void deferredDocumentStillRenders();
    void smallDocumentRendersImmediately();
    void inlineMathBecomesADrawnObject();
    void sourceLineTracksTheBlock();
    void clickingThePreviewReportsTheSourceLine();
    void documentPointerChangesAcrossRebuilds();
    void handlersStayRegisteredAcrossManyRebuilds();
    void gifAnimationsSurviveRebuilds();
    void centeredBadgeRowStaysCenteredAfterSwap();
    void reopeningTheDocumentRendersTheBadgeRowCorrectlyAgain();
    void remoteImagesDisabledRendersTheAltChip();
    void remoteImagesEnabledInsertTheImageObject();
    void failedRemoteImageFallsBackToTheAltChip();
    void remoteImageAppearsAfterDownload();
    void htmlPercentImageFitsThePaneAndReshrinks();
    void htmlTableImagesScaleToCellWidthAndDoNotOverflow();
    void remoteImageIsPreScaledToTheDrawnWidth();
    void hoveringALinkShowsTheHandCursor();
};

void ViewTest::rendersMarkdownIntoDocument()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("# Title\n\ntext\n"));

    const QString text = view.document()->toPlainText();
    QVERIFY(text.contains(QStringLiteral("Title")));
    QVERIFY(text.contains(QStringLiteral("text")));
}

void ViewTest::inlineMathBecomesADrawnObject()
{
    MarkdownView view;
    view.resize(600, 400);
    view.show();
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("Euler: $e^{i\\pi} + 1 = 0$ indeed\n"));

    // The formula arrives as an object replacement character carrying the TeX
    // source; the handler registered by the view draws it, so rendering the
    // pane must produce ink where the formula sits -- not the raw source.
    bool hasMathObject = false;
    for (QTextBlock block = view.document()->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().isValid() && it.fragment().charFormat().objectType() == MarkdownBuilder::mathObjectType)
                hasMathObject = true;
        }
    }
    QVERIFY(hasMathObject);
    QVERIFY(!view.document()->toPlainText().contains(QStringLiteral("e^{i\\pi}")));

    const QImage image = view.viewport()->grab().toImage();
    int ink = 0;
    for (int y = 0; y < image.height(); y += 2)
        for (int x = 0; x < image.width(); x += 2)
            if (image.pixel(x, y) != QColor(Qt::white).rgb() && qAlpha(image.pixel(x, y)) > 0)
                ++ink;
    QVERIFY2(ink > 20, "the pane shows no drawn formula");
}

void ViewTest::previewHighlightsFencedCode()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("```cpp\n// note\nint x = 0;\n```\n"));

    QSet<QRgb> colors;
    for (QTextBlock block = view.document()->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it)
            colors.insert(it.fragment().charFormat().foreground().color().rgb());
    }
    // The comment and the keyword got colors distinct from plain code text.
    QVERIFY(colors.size() >= 3);
}

void ViewTest::themeSwitchRewritesDocument()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("# Title\n"));

    RenderStyle style;
    style.theme = ThemeDefinition::light();
    view.setRenderStyle(style);
    QVERIFY(view.document()->firstBlock().charFormat().foreground().color() == ThemeDefinition::light().markdown.heading);

    style.theme = ThemeDefinition::dark();
    view.setRenderStyle(style);
    QCOMPARE(view.document()->firstBlock().charFormat().foreground().color(), ThemeDefinition::dark().markdown.heading);
}

void ViewTest::burstOfChangesCoalescesIntoOneRender()
{
    MarkdownView view;
    view.setRenderInterval(50);

    QSignalSpy spy(&view, &MarkdownView::rendered);
    for (int i = 0; i < 5; ++i)
        view.setMarkdown(QStringLiteral("# Title %1\n").arg(i));

    QCOMPARE(spy.count(), 0); // nothing yet: the render is coalesced
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
    QVERIFY(view.document()->toPlainText().contains(QStringLiteral("Title 4")));
}

void ViewTest::renderIsImmediateWhenIntervalIsZero()
{
    MarkdownView view;
    view.setRenderInterval(0);
    QSignalSpy spy(&view, &MarkdownView::rendered);
    view.setMarkdown(QStringLiteral("# Now\n"));
    QCOMPARE(spy.count(), 1);
}

void ViewTest::scrollIsPreservedAcrossRerender()
{
    MarkdownView view;
    view.resize(400, 300);
    view.show();
    view.setRenderInterval(0);

    QString body;
    for (int i = 0; i < 200; ++i)
        body += QStringLiteral("line %1\n\n").arg(i);
    view.setMarkdown(body);

    view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum() / 2);
    const int before = view.verticalScrollBar()->value();
    const QString topBefore = view.cursorForPosition(QPoint(0, 0)).block().text();
    QVERIFY(before > 0);

    // Append more text: the block at the top must stay at the top.
    view.setMarkdown(body + QStringLiteral("added at the end\n"));
    const QString topAfter = view.cursorForPosition(QPoint(0, 0)).block().text();
    QCOMPARE(topAfter, topBefore);
}

void ViewTest::baseDirResolvesImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile png(dir.filePath(QStringLiteral("pic.png")));
    QVERIFY(png.open(QIODevice::WriteOnly));
    png.write(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="));
    png.close();

    MarkdownView view;
    view.setRenderInterval(0);
    view.setBaseDir(dir.path());
    view.setMarkdown(QStringLiteral("![alt](pic.png)\n"));

    bool found = false;
    for (QTextBlock block = view.document()->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                found = true;
        }
    }
    QVERIFY(found);
}

void ViewTest::contentIsCappedByMaxWidth()
{
    MarkdownView view;
    view.resize(2000, 600); // much wider than the cap
    view.show();
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("A paragraph long enough to occupy the column.\n"));

    QTRY_VERIFY(view.document()->textWidth() > 0);
    // The rendered column must not stretch across the whole window; give the
    // layout a chance to apply the margins.
    QTRY_VERIFY_WITH_TIMEOUT(view.document()->textWidth() <= view.renderStyle().maxContentWidth + 4, 3000);
}

void ViewTest::htmlPercentImageFitsThePaneAndReshrinks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile png(dir.filePath(QStringLiteral("pic.png")));
    QVERIFY(png.open(QIODevice::WriteOnly));
    png.write(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="));
    png.close();

    MarkdownView view;
    view.resize(900, 600);
    view.show();
    view.setRenderInterval(0);
    view.setBaseDir(dir.path());
    view.setMarkdown(QStringLiteral("<img src=\"pic.png\" alt=\"x\" width=\"100%\">\n"));

    const auto imageWidth = [&view] {
        for (QTextBlock block = view.document()->firstBlock(); block.isValid(); block = block.next())
            for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().isImageFormat())
                    return int(it.fragment().charFormat().toImageFormat().width());
        return 0;
    };

    // width="100%" must resolve to the pane's column, not the column *cap*.
    QTRY_VERIFY(view.document()->textWidth() > 0);
    QTRY_VERIFY(imageWidth() > 0);
    const int wide = imageWidth();
    QVERIFY2(wide <= int(view.document()->textWidth()) + 4,
             qPrintable(QStringLiteral("image %1 wider than column %2").arg(wide).arg(view.document()->textWidth())));
    QVERIFY(wide > 400); // a full-width hero, not the old 100 px

    // A narrower pane (like an open editor) must re-render so the image fits.
    view.resize(500, 600);
    QTRY_VERIFY_WITH_TIMEOUT(imageWidth() > 0 && imageWidth() < wide, 3000);
    QVERIFY(imageWidth() <= int(view.document()->textWidth()) + 4);
}

void ViewTest::htmlTableImagesScaleToCellWidthAndDoNotOverflow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Create a 1000x500 test image
    QImage testImg(1000, 500, QImage::Format_ARGB32);
    testImg.fill(Qt::blue);
    QVERIFY(testImg.save(dir.filePath(QStringLiteral("test.png")), "PNG"));

    MarkdownView view;
    view.resize(900, 600);
    view.show();
    view.setRenderInterval(0);
    view.setBaseDir(dir.path());

    // 2-column table with 50% width cells and 100% width images, like in README.md
    const QString md = QStringLiteral(
        "<table>\n"
        "<tr>\n"
        "<td width=\"50%\" valign=\"top\"><img src=\"test.png\" alt=\"left\" width=\"100%\"></td>\n"
        "<td width=\"50%\" valign=\"top\"><img src=\"test.png\" alt=\"right\" width=\"100%\"></td>\n"
        "</tr>\n"
        "</table>\n");
    view.setMarkdown(md);

    QTRY_VERIFY(view.document()->textWidth() > 0);

    QVector<int> widths;
    for (QTextBlock block = view.document()->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                widths.append(int(it.fragment().charFormat().toImageFormat().width()));
        }
    }

    QCOMPARE(widths.size(), 2);
    const int colWidth = int(view.document()->textWidth() / 2);
    for (int w : widths) {
        QVERIFY2(w > 100, qPrintable(QStringLiteral("image width %1 too small").arg(w)));
        QVERIFY2(w <= colWidth, qPrintable(QStringLiteral("image width %1 exceeds column width %2").arg(w).arg(colWidth)));
    }
}

void ViewTest::codeFramesAreFoundAfterRender()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("```cpp\nint x = 0;\n```\n"));

    const QVector<QTextFrame *> frames = view.codeFrames();
    QCOMPARE(frames.size(), 1);
    QCOMPARE(view.codeText(frames.first()), QStringLiteral("int x = 0;"));
}

void ViewTest::copyButtonCopiesTheCode()
{
    MarkdownView view;
    view.resize(700, 500);
    view.show();
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("```cpp\nint x = 0;\nint y = 1;\n```\n"));

    QTRY_VERIFY(!view.codeFrames().isEmpty());
    QTextFrame *frame = view.codeFrames().first();

    // The layout must settle before the button has a rectangle.
    QRect button;
    for (int attempt = 0; attempt < 50 && button.isEmpty(); ++attempt) {
        QTest::qWait(20);
        button = view.copyButtonRect(frame);
    }
    QVERIFY(!button.isEmpty());

    // Hover the code box, then click its copy button.
    QTest::mouseMove(view.viewport(), button.center());
    QVERIFY(view.copyCodeAt(button.center()));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("int x = 0;\nint y = 1;"));

    // A click elsewhere in the box must not copy.
    QVERIFY(!view.copyCodeAt(view.codeFrameRect(frame).topLeft() + QPoint(20, 20)));
}

void ViewTest::anchorScrollsInsteadOfBlankTheView()
{
    MarkdownView view;
    view.resize(700, 300);
    view.show();
    view.setRenderInterval(0);

    QString markdown = QStringLiteral("# Intro\n\n");
    for (int i = 0; i < 80; ++i)
        markdown += QStringLiteral("line %1\n\n").arg(i);
    markdown += QStringLiteral("# Key features\n\nbody\n");
    view.setMarkdown(markdown);
    QTRY_VERIFY(view.document()->blockCount() > 80);

    // A known anchor scrolls to the heading...
    QVERIFY(view.scrollToAnchor(QStringLiteral("key-features")));
    QVERIFY(view.verticalScrollBar()->value() > 0);

    // ...and an unknown one does nothing at all (never blanks the view).
    const int value = view.verticalScrollBar()->value();
    QVERIFY(!view.scrollToAnchor(QStringLiteral("does-not-exist")));
    QCOMPARE(view.verticalScrollBar()->value(), value);
    QVERIFY(!view.document()->isEmpty());
}

void ViewTest::hugeDocumentPausesThePreview()
{
    MarkdownView view;
    view.setDocumentLimits(DocumentLimits::forSize(8 * 1024 * 1024)); // Manual tier
    QVERIFY(view.previewPaused());

    view.setMarkdown(QStringLiteral("# Title\n\ntext\n"));

    const QString shown = view.document()->toPlainText();
    QVERIFY(shown.contains(QStringLiteral("Preview paused")));
    QVERIFY(!shown.contains(QStringLiteral("text")));
    QCOMPARE(view.lastRenderMs(), 0); // nothing was rendered
}

void ViewTest::renderAnywayOverridesThePause()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setDocumentLimits(DocumentLimits::forSize(8 * 1024 * 1024));
    view.setMarkdown(QStringLiteral("# Title\n\ntext\n"));
    QVERIFY(view.document()->toPlainText().contains(QStringLiteral("Preview paused")));

    view.renderAnyway();

    QVERIFY(!view.previewPaused());
    QVERIFY(view.document()->toPlainText().contains(QStringLiteral("text")));

    // The next edit gives the pause back: a huge file is never live.
    view.setMarkdown(QStringLiteral("# Title\n\nedited\n"));
    QTRY_VERIFY_WITH_TIMEOUT(view.document()->toPlainText().contains(QStringLiteral("Preview paused")), 3000);
}

void ViewTest::deferredDocumentStillRenders()
{
    MarkdownView view;
    view.setDocumentLimits(DocumentLimits::forSize(2 * 1024 * 1024)); // Deferred tier
    QVERIFY(!view.previewPaused());

    QSignalSpy spy(&view, &MarkdownView::rendered);
    view.setMarkdown(QStringLiteral("# Title\n"));
    QCOMPARE(spy.count(), 0); // held back until the typing pauses

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);
    QVERIFY(view.document()->toPlainText().contains(QStringLiteral("Title")));
    QVERIFY(view.lastRenderMs() >= 0);
}

void ViewTest::smallDocumentRendersImmediately()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setDocumentLimits(DocumentLimits{}); // Interactive: nothing held back
    QVERIFY(!view.previewPaused());

    view.setMarkdown(QStringLiteral("# Title\n"));
    QVERIFY(view.document()->toPlainText().contains(QStringLiteral("Title")));
}

void ViewTest::sourceLineTracksTheBlock()
{
    MarkdownView view;
    view.resize(600, 400);
    view.show();
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("# Title\n\nplain text line\n\n- list item\n"));

    const auto lineAtBlock = [&view](const QString &text) {
        for (QTextBlock block = view.document()->firstBlock(); block.isValid(); block = block.next()) {
            if (block.text() != text)
                continue;
            return view.sourceLineAt(view.cursorRect(QTextCursor(block)).center());
        }
        return -1;
    };

    QCOMPARE(lineAtBlock(QStringLiteral("Title")), 0);
    QCOMPARE(lineAtBlock(QStringLiteral("plain text line")), 2);
    QCOMPARE(lineAtBlock(QStringLiteral("list item")), 4);
}

void ViewTest::clickingThePreviewReportsTheSourceLine()
{
    MarkdownView view;
    view.resize(600, 400);
    view.show();
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("# Title\n\nplain text line\n"));

    QTextBlock target;
    for (QTextBlock block = view.document()->firstBlock(); block.isValid(); block = block.next()) {
        if (block.text() == QLatin1String("plain text line"))
            target = block;
    }
    QVERIFY(target.isValid());

    QSignalSpy spy(&view, &MarkdownView::sourceLineActivated);
    const QPoint at = view.cursorRect(QTextCursor(target)).center();
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, at);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 2);
}

// The preview builds into a document that no widget watches, then swaps it in
// atomically: building into the live document interleaves QTextDocumentLayout
// work with every cursor insertion, which is where whole open-file delays came
// from. A fresh build lands in a different document object each time.
void ViewTest::documentPointerChangesAcrossRebuilds()
{
    MarkdownView view;
    view.setRenderInterval(0);

    view.setMarkdown(QStringLiteral("# first\n\nsome text\n"));
    const QTextDocument *first = view.document();
    QVERIFY(view.document()->firstBlock().isValid());
    QVERIFY(view.document()->toPlainText().contains(QLatin1String("some text")));

    // Each render swaps in the other document of the pair: never the one that
    // is showing right now.
    view.setMarkdown(QStringLiteral("# second\n\nother text\n"));
    QVERIFY(view.document() != first);
    QVERIFY(view.document()->toPlainText().contains(QLatin1String("other text")));
    QVERIFY(!view.document()->toPlainText().contains(QLatin1String("some text")));

    // The third render swaps back -- and must still hand the view a fully
    // built document, not an empty one.
    const QTextDocument *second = view.document();
    view.setMarkdown(QStringLiteral("# third\n\nmore text\n"));
    QVERIFY(view.document() != second);
    QVERIFY(view.document()->toPlainText().contains(QLatin1String("more text")));
}

void ViewTest::handlersStayRegisteredAcrossManyRebuilds()
{
    MarkdownView view;
    view.setRenderInterval(0);

    const QVector<int> objectTypes = {
        MarkdownBuilder::mathObjectType,
        MarkdownBuilder::checkboxObjectType,
        MarkdownBuilder::chipObjectType,
        MarkdownBuilder::gifObjectType,
    };

    for (int round = 0; round < 6; ++round) {
        view.setMarkdown(QStringLiteral("# round %1\n\n$x^2$ and - [ ] a task\n\n![badge](https://example.com/badge.svg)\n").arg(round));
        QAbstractTextDocumentLayout *layout = view.document()->documentLayout();
        for (const int type : objectTypes)
            QVERIFY2(layout->handlerForObject(type) != nullptr, qPrintable(QStringLiteral("round %1: handler for type %2 missing").arg(round).arg(type)));
    }
}

static const char *kAnimatedGifBase64 =
    "R0lGODlhBAAEAIEAAP8AAAAAAAAAAAAAACH/"
    "C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAABAAEAAAICQABCBxIsCCAgAAh+QQBCgABACwAAAAABAAEAIEAAP8AAAAAAAAAAAAICQABCBxIsCCAgAA7";

// The movie objects live with the view, not with any single document: swapping
// documents on every render must not stop the animation of a GIF that is still
// on screen, and the handler must repaint into the document now showing.
void ViewTest::gifAnimationsSurviveRebuilds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile gif(dir.filePath(QStringLiteral("anim.gif")));
    QVERIFY(gif.open(QIODevice::WriteOnly));
    gif.write(QByteArray::fromBase64(kAnimatedGifBase64));
    gif.close();

    MarkdownView view;
    view.resize(400, 300);
    view.show();
    view.setRenderInterval(0);
    view.setBaseDir(dir.path());
    view.setMarkdown(QStringLiteral("# gif\n\n![anim](anim.gif)\n"));

    auto *handler = view.findChild<GifObjectHandler *>();
    QVERIFY(handler);
    QTRY_VERIFY_WITH_TIMEOUT(handler->hasRunningAnimations(), 3000);

    view.setMarkdown(QStringLiteral("# gif\n\n![anim](anim.gif)\n"));
    QTRY_VERIFY_WITH_TIMEOUT(handler->hasRunningAnimations(), 3000);
}

// README.md renders its shields.io badges as a centred row of chips. That row
// is the only content in the app that makes a swap-time layout fault visible:
// QTextEdit's own setDocument machinery measures the fresh document for the
// scrollbar ranges before the rebuild gets a chance to register the object
// handlers, an unhandled object measures as zero width, and the centred line
// then sits at the middle of the column with the chips drawn over its right
// edge. Resizing repaints correctly (a full relayout with handlers in place),
// reopening breaks it again -- exactly the report that led here.
static QString badgeDocument()
{
    return QStringLiteral(
        "<div align=\"center\">\n"
        "\n"
        "[![alpha](https://img.shields.io/badge/alpha.svg)](https://example.com/a)\n"
        "[![beta](https://img.shields.io/badge/beta.svg)](https://example.com/b)\n"
        "[![gamma](https://img.shields.io/badge/gamma.svg)](https://example.com/g)\n"
        "\n"
        "</div>\n");
}

static QTextBlock chipBlockIn(const QTextDocument *document)
{
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().objectType() == MarkdownBuilder::chipObjectType)
                return block;
        }
    }
    return QTextBlock();
}

static void checkBadgeRowIsCentred(const MarkdownView &view)
{
    const QTextBlock block = chipBlockIn(view.document());
    QVERIFY2(block.isValid(), "the badge paragraph is missing from the document");
    QVERIFY2(block.layout()->lineCount() > 0, "the badge row was never laid out");

    const QTextLine line = block.layout()->lineAt(0);
    QVERIFY2(line.naturalTextWidth() > 100,
             "the chips row was measured as near-zero width: objects went through "
             "the layout without their handler registered");

    // The row must sit centred in the column, not run off its right edge.
    const qreal left = view.cursorRect(QTextCursor(block)).left();
    const qreal column = view.viewport()->width();
    const qreal expected = (column - line.naturalTextWidth()) / 2.0;
    QVERIFY2(
        qAbs(left - expected) < 12.0,
        qPrintable(QStringLiteral("row left %1, expected ~%2 (column %3, row width %4)").arg(left).arg(expected).arg(column).arg(line.naturalTextWidth())));
    QVERIFY2(left + line.naturalTextWidth() <= column + 2.0, "the chips row runs past the right edge of the column");
}

void ViewTest::centeredBadgeRowStaysCenteredAfterSwap()
{
    MarkdownView view;
    view.resize(700, 400);
    view.show();
    view.setRenderInterval(0);
    view.setMarkdown(badgeDocument());
    checkBadgeRowIsCentred(view);
}

void ViewTest::reopeningTheDocumentRendersTheBadgeRowCorrectlyAgain()
{
    MarkdownView view;
    view.resize(700, 400);
    view.show();
    view.setRenderInterval(0);

    // Open something else, then reopen the badges: every render swaps in a
    // fresh document, so the fault must not depend on which render is first.
    view.setMarkdown(QStringLiteral("# other\n\nplain text\n"));
    view.setMarkdown(badgeDocument());
    checkBadgeRowIsCentred(view);
    view.setMarkdown(QStringLiteral("# other\n\nplain text\n"));
    view.setMarkdown(badgeDocument());
    checkBadgeRowIsCentred(view);
}

static QTextImageFormat imageObjectIn(const QTextDocument *document)
{
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.isImageFormat())
                return format.toImageFormat();
        }
    }
    return QTextImageFormat();
}

static bool hasChipObject(const QTextDocument *document)
{
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().objectType() == MarkdownBuilder::chipObjectType)
                return true;
        }
    }
    return false;
}

void ViewTest::remoteImagesDisabledRendersTheAltChip()
{
    MarkdownView view;
    view.setRenderInterval(0);
    // Remote loading is off by default; the alt text carries the meaning.
    QVERIFY(!view.remoteImagesEnabled());

    view.setMarkdown(QStringLiteral("![badge](https://example.com/badge.svg)\n"));
    QVERIFY(hasChipObject(view.document()));
    QVERIFY(imageObjectIn(view.document()).name().isEmpty());
}

void ViewTest::remoteImagesEnabledInsertTheImageObject()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setRemoteImagesEnabled(true);

    // A loopback URL: the fetcher refuses it immediately (private network),
    // so this exercises the object path without any real network traffic.
    const QString url = QStringLiteral("http://127.0.0.1:9/badge.svg");
    view.setMarkdown(QStringLiteral("![badge](%1)\n").arg(url));
    view.renderNow();

    const QTextImageFormat format = imageObjectIn(view.document());
    QCOMPARE(format.name(), url);
    QCOMPARE(format.property(MarkdownBuilder::imageAltKey).toString(), QStringLiteral("badge"));
    QVERIFY(!hasChipObject(view.document()));
}

void ViewTest::failedRemoteImageFallsBackToTheAltChip()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setRemoteImagesEnabled(true);

    const QString url = QStringLiteral("http://127.0.0.1:9/badge.svg");
    view.setMarkdown(QStringLiteral("![badge](%1)\n").arg(url));
    view.renderNow();

    // The refusal is immediate; the block then settles on the alt-text chip
    // drawn into the resource slot, instead of staying an empty gap.
    QTRY_VERIFY(!view.document()->resource(QTextDocument::ImageResource, QUrl(url)).isNull());
}

void ViewTest::remoteImageAppearsAfterDownload()
{
    TestHttpServer server;
    QVERIFY(server.isListening());
    server.setRoute(QStringLiteral("/pic.png"),
                    {200,
                     {{"Content-Type", "image/png"}},
                     QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="),
                     true});

    QTemporaryDir cache;
    MarkdownView view;
    view.resize(400, 300);
    view.show();
    view.setRenderInterval(0);
    view.setRemoteImagesEnabled(true);
    view.imageFetcher()->setCacheDirectory(cache.path());
    view.imageFetcher()->setPrivateNetworkAllowed(true);

    const QUrl url = server.url(QStringLiteral("/pic.png"));
    view.setMarkdown(QStringLiteral("![pic](%1)\n").arg(url.toString()));
    view.renderNow();

    QTRY_VERIFY(!view.document()->resource(QTextDocument::ImageResource, url).isNull());
    const QImage image = view.document()->resource(QTextDocument::ImageResource, url).value<QImage>();
    QCOMPARE(image.width(), 1);
    QCOMPARE(image.height(), 1);
}

void ViewTest::remoteImageIsPreScaledToTheDrawnWidth()
{
    TestHttpServer server;
    QVERIFY(server.isListening());

    QImage source(40, 20, QImage::Format_ARGB32);
    source.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(source.save(&buffer, "PNG"));
    buffer.close();
    server.setRoute(QStringLiteral("/pic.png"), {200, {{"Content-Type", "image/png"}}, bytes, true});

    QTemporaryDir cache;
    MarkdownView view;
    view.resize(400, 300);
    view.show();
    view.setRenderInterval(0);
    view.setRemoteImagesEnabled(true);
    view.imageFetcher()->setCacheDirectory(cache.path());
    view.imageFetcher()->setPrivateNetworkAllowed(true);

    const QUrl url = server.url(QStringLiteral("/pic.png"));
    view.setMarkdown(QStringLiteral("<img src=\"%1\" alt=\"p\" width=\"20\">\n").arg(url.toString()));
    view.renderNow();

    QTRY_VERIFY(!view.document()->resource(QTextDocument::ImageResource, url).isNull());
    const QImage image = view.document()->resource(QTextDocument::ImageResource, url).value<QImage>();
    // Pre-scaled to the drawn width (20 logical), not left at the 40px source,
    // so the text layout paints it 1:1 instead of resampling it.
    QCOMPARE(image.deviceIndependentSize().toSize(), QSize(20, 10));
}

void ViewTest::hoveringALinkShowsTheHandCursor()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage badge(48, 20, QImage::Format_ARGB32);
    badge.fill(Qt::red);
    QVERIFY(badge.save(dir.filePath(QStringLiteral("badge.png"))));

    MarkdownView view;
    view.resize(700, 400);
    view.show();
    view.setRenderInterval(0);
    view.setBaseDir(dir.path());
    view.setMarkdown(QStringLiteral("See [the docs](https://example.com) here.\n\n[![badge](badge.png)](https://example.com)\n"));
    QTRY_VERIFY(view.document()->blockCount() >= 2);

    // Find the badge object: an image carrying a link.
    int badgePosition = -1;
    for (QTextBlock block = view.document()->firstBlock(); block.isValid() && badgePosition < 0; block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                badgePosition = it.fragment().position();
        }
    }
    QVERIFY(badgePosition >= 0);

    const auto hover = [&view](int documentPosition) {
        QTextCursor cursor(view.document());
        cursor.setPosition(documentPosition);
        const QPoint center = view.cursorRect(cursor).center();
        QMouseEvent move(QEvent::MouseMove,
                         QPointF(center),
                         QPointF(center),
                         QPointF(view.viewport()->mapToGlobal(center)),
                         Qt::NoButton,
                         Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &move);
    };

    const QTextBlock firstBlock = view.document()->firstBlock();
    const int linkStart = firstBlock.position() + int(firstBlock.text().indexOf(QStringLiteral("the docs")));
    QVERIFY(linkStart > firstBlock.position());

    hover(linkStart + 2); // inside the linked text
    QCOMPARE(view.viewport()->cursor().shape(), Qt::PointingHandCursor);

    hover(badgePosition); // the linked image object
    QCOMPARE(view.viewport()->cursor().shape(), Qt::PointingHandCursor);

    hover(firstBlock.position() + 1); // plain text
    QCOMPARE(view.viewport()->cursor().shape(), Qt::IBeamCursor);

    // Leaving the view must not leave the hand cursor behind.
    hover(linkStart + 2);
    QCOMPARE(view.viewport()->cursor().shape(), Qt::PointingHandCursor);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&view, &leave);
    QCOMPARE(view.viewport()->cursor().shape(), Qt::IBeamCursor);
}

QTEST_MAIN(ViewTest)
#include "test_view.moc"