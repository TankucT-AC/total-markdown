// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "core/FontManager.h"
#include "core/LocalImageCache.h"
#include "core/MarkdownBuilder.h"
#include "core/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QThread>
#include <QUrl>

#include <functional>

class MarkdownTest : public QObject
{
    Q_OBJECT

private slots:
    void headingLevelAndScale();
    void headingSixUsesWeakColor();
    void codeBlockIsOneContinuousRegion();
    void wrappedCodeLineStaysInsideTheBox();
    void htmlDivCentersContent();
    void htmlSummaryIsRendered();
    void htmlInlineTagsAreApplied();
    void htmlKbdAndRemoteImageBecomeChips();
    void calloutIsRendered();
    void headingSlugsMatchGithubStyle();
    void htmlImageHonoursWidthAttribute();
    void htmlPercentWidthFillsTheContentColumn();
    void htmlPercentWidthOnRemoteImageIsSetBeforeLoad();
    void htmlWidthWithoutDigitsIsIgnored();
    void htmlPercentWidthFollowsTheLiveColumn();
    void htmlBlockImageIsNotStretchedToTheLineHeight();
    void standaloneBreakDoesNotMergeIntoTheNextImage();
    void imageInsideALinkIsClickable();
    void htmlImageInsideCenteredDivIsCentered();
    void htmlParagraphAlignCentersMarkdownBlocks();
    void htmlTableCellsRenderTheirMarkdown();
    void htmlTableWithBlankLinesBecomesATable();
    void htmlTableImageIsConstrainedByCellWidth();
    void gfmTableImageIsConstrainedByCellWidth();
    void htmlBadgeRowStaysOnOneLine();
    void readmeRendersWithoutLiteralHtml();
    void boldEmphasisInParagraph();
    void inlineCodeSpan();
    void fencedCodeBlockCarriesLanguage();
    void bulletList();
    void orderedList();
    void taskList();
    void blockQuote();
    void linkSpan();
    void strikethroughSpan();
    void gfmTable();
    void tableGridLinesUseTheBorderColor();
    void imageResolvesAgainstBaseDir();
    void animatedImageBecomesObject();
    void svgIsRasterisedWithAntialiasing();
    void horizontalRule();
    void entityDecoding();
    void softBreakStaysOneParagraph();
    void emptyDocument();
    void missingImageShowsPlaceholder();
    void inlineMathBecomesAnObject();
    void displayMathIsMarkedAndCentred();
    void displayMathDoesNotStealTheNeighbourParagraph();
    void escapedDollarStaysLiteral();
    void mathInsideCodeStaysLiteral();
    void blocksRecordTheirSourceLine();
    void rulesLinksAndImagesMapToTheirLine();
    void localImageCacheCachesAndInvalidatesOnMtime();
    void localImageCacheFastDownsampling();
    void repeatedRendersReuseCachedImages();

private:
    struct FoundMath {
        QString source;
        bool display = false;
    };

    void render(QTextDocument *document, const QString &markdown, const QString &baseDir = QString());
    static QTextCharFormat formatAt(const QTextBlock &block, int position);
    static QImage renderImage(QTextDocument *document, int width);
    static int codeLeftEdge(const QImage &image, int row, QRgb code);
    static const QColor &codeColor();
    static QVector<FoundMath> mathObjectsIn(const QTextDocument *document);
};

const QColor &MarkdownTest::codeColor()
{
    static const QColor color = ThemeDefinition::light().markdown.codeBackground;
    return color;
}

QImage MarkdownTest::renderImage(QTextDocument *document, int width)
{
    document->setTextWidth(width);
    const int height = int(document->size().height()) + 2;
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    document->drawContents(&painter, QRectF(0, 0, width, height));
    painter.end();
    return image;
}

int MarkdownTest::codeLeftEdge(const QImage &image, int row, QRgb code)
{
    for (int x = 0; x < image.width(); ++x) {
        if (image.pixel(x, row) == code)
            return x;
    }
    return -1;
}

void MarkdownTest::render(QTextDocument *document, const QString &markdown, const QString &baseDir)
{
    RenderStyle style;
    style.theme = ThemeDefinition::light();
    style.baseDir = baseDir;

    QVERIFY(MarkdownBuilder::build(document, markdown, style));
}

QTextCharFormat MarkdownTest::formatAt(const QTextBlock &block, int position)
{
    const int absolute = block.position() + position;
    for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (fragment.contains(absolute))
            return fragment.charFormat();
    }
    return QTextCharFormat();
}

void MarkdownTest::headingLevelAndScale()
{
    QTextDocument document;
    render(&document, QStringLiteral("# Title\n"));
    const QTextBlock block = document.firstBlock();
    QCOMPARE(block.blockFormat().intProperty(MarkdownBuilder::kindKey), int(MarkdownBuilder::BlockKind::Heading));
    QCOMPARE(block.blockFormat().intProperty(MarkdownBuilder::headingLevelKey), 1);
    QCOMPARE(block.text(), QStringLiteral("Title"));

    const qreal bodySize = FontManager().bodyFont().pointSizeF();
    QVERIFY(block.charFormat().fontPointSize() > bodySize);
    QVERIFY(block.charFormat().font().bold());
}

void MarkdownTest::headingSixUsesWeakColor()
{
    const ThemeDefinition theme = ThemeDefinition::light();
    QTextDocument document;
    render(&document, QStringLiteral("###### Small\n"));
    const QTextBlock block = document.firstBlock();
    QCOMPARE(block.blockFormat().intProperty(MarkdownBuilder::headingLevelKey), 6);
    QCOMPARE(block.charFormat().foreground().color(), theme.markdown.weakText);
}

void MarkdownTest::boldEmphasisInParagraph()
{
    QTextDocument document;
    render(&document, QStringLiteral("plain **bold** text\n"));
    const QTextBlock block = document.firstBlock();
    QCOMPARE(block.text(), QStringLiteral("plain bold text"));

    QTextCharFormat boldFormat;
    boldFormat.setFontWeight(QFont::Bold);
    QVERIFY(formatAt(block, int(block.text().indexOf(QStringLiteral("bold")))).fontWeight() == QFont::Bold);
    QVERIFY(formatAt(block, 0).fontWeight() == QFont::Normal);
}

void MarkdownTest::codeBlockIsOneContinuousRegion()
{
    QTextDocument document;
    render(&document, QStringLiteral("```\nline1\n\nline2\n```\n"));
    document.setTextWidth(600);

    const int height = int(document.size().height()) + 2;
    QImage image(600, height, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    document.drawContents(&painter, QRectF(0, 0, 600, height));
    painter.end();

    const QRgb code = ThemeDefinition::light().markdown.codeBackground.rgb();

    int first = -1;
    int last = -1;
    for (int y = 0; y < height; ++y) {
        if (image.pixel(300, y) == code) {
            if (first < 0)
                first = y;
            last = y;
        }
    }
    QVERIFY(first >= 0);

    // No unshaded row between the first and last row of the block: a blank
    // line inside the fence must not split it into separate boxes.
    for (int y = first; y <= last; ++y)
        QCOMPARE(image.pixel(300, y), code);

    // The shaded region spans (almost) the whole content width.
    int left = -1;
    int right = -1;
    for (int x = 0; x < 600; ++x) {
        if (image.pixel(x, (first + last) / 2) == code) {
            if (left < 0)
                left = x;
            right = x;
        }
    }
    QVERIFY(right - left > 400);
}

void MarkdownTest::wrappedCodeLineStaysInsideTheBox()
{
    QTextDocument document;
    render(&document, QStringLiteral("```\nQElapsedTimer m_frameTimer; // a very long comment that must wrap somewhere across lines\nshort;\n```\n"));

    // Narrow width forces the long line to wrap.
    const QImage image = renderImage(&document, 320);
    const QRgb code = codeColor().rgb();

    int first = -1;
    int last = -1;
    for (int y = 0; y < image.height(); ++y) {
        if (image.pixel(image.width() / 2, y) == code) {
            if (first < 0)
                first = y;
            last = y;
        }
    }
    QVERIFY(first >= 0);

    // The shaded box must be a rectangle: its left edge is the same on every
    // row, so a wrapped continuation line never spills out to the left.
    const int edge = codeLeftEdge(image, first, code);
    for (int y = first; y <= last; ++y)
        QCOMPARE(codeLeftEdge(image, y, code), edge);
}

void MarkdownTest::inlineCodeSpan()
{
    QTextDocument document;
    render(&document, QStringLiteral("a `code` b\n"));
    const QTextBlock block = document.firstBlock();
    const int at = int(block.text().indexOf(QStringLiteral("code")));

    const QTextCharFormat format = formatAt(block, at);
    QCOMPARE(format.font().family(), QStringLiteral("JetBrains Mono"));
    QCOMPARE(format.background().color(), ThemeDefinition::light().markdown.codeBackground);
}

void MarkdownTest::fencedCodeBlockCarriesLanguage()
{
    QTextDocument document;
    render(&document, QStringLiteral("```cpp\nint x = 0;\n```\n"));

    QTextBlock block;
    for (QTextBlock candidate = document.firstBlock(); candidate.isValid(); candidate = candidate.next()) {
        if (candidate.blockFormat().intProperty(MarkdownBuilder::kindKey) == int(MarkdownBuilder::BlockKind::Code)) {
            block = candidate;
            break;
        }
    }
    QVERIFY(block.isValid());

    QCOMPARE(block.blockFormat().intProperty(MarkdownBuilder::kindKey), int(MarkdownBuilder::BlockKind::Code));
    QCOMPARE(block.blockFormat().stringProperty(MarkdownBuilder::languageKey), QStringLiteral("cpp"));
    QCOMPARE(block.text(), QStringLiteral("int x = 0;"));
    QVERIFY(QFontInfo(block.charFormat().font()).fixedPitch());
}

void MarkdownTest::bulletList()
{
    QTextDocument document;
    render(&document, QStringLiteral("- one\n- two\n"));
    QTextList *list = nullptr;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        if (QTextList *candidate = block.textList()) {
            list = candidate;
            break;
        }
    }
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->format().style(), QTextListFormat::ListDisc);
}

void MarkdownTest::orderedList()
{
    QTextDocument document;
    render(&document, QStringLiteral("1. one\n2. two\n"));
    QTextList *list = nullptr;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        if (QTextList *candidate = block.textList()) {
            list = candidate;
            break;
        }
    }
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->format().style(), QTextListFormat::ListDecimal);
}

void MarkdownTest::taskList()
{
    QTextDocument document;
    render(&document, QStringLiteral("- [ ] todo\n- [x] done\n"));

    QString todo;
    QString done;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        if (block.text().contains(QStringLiteral("todo")))
            todo = block.text();
        if (block.text().contains(QStringLiteral("done")))
            done = block.text();
    }
    // The marker is a checkbox object, so the leading glyph is the object
    // replacement character; the label text follows it.
    QVERIFY(todo.contains(QStringLiteral("todo")));
    QVERIFY(done.contains(QStringLiteral("done")));

    // Checked and unchecked items carry the checkbox object type; the checked
    // one has its "checked" property set.
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.objectType() != MarkdownBuilder::checkboxObjectType)
                continue;
            const bool checked = format.property(MarkdownBuilder::checkboxCheckedKey).toBool();
            if (block.text().contains(QStringLiteral("done")))
                QVERIFY(checked);
            if (block.text().contains(QStringLiteral("todo")))
                QVERIFY(!checked);
        }
        // Done items must NOT be struck through any more.
        const int at = int(block.text().indexOf(QStringLiteral("done")));
        if (at >= 0)
            QVERIFY(!formatAt(block, at).fontStrikeOut());
    }
}

void MarkdownTest::blockQuote()
{
    QTextDocument document;
    render(&document, QStringLiteral("> quoted\n"));

    // The quote is a one-cell table with a left bar; find it and check the bar.
    bool found = false;
    for (QTextFrame *frame : document.rootFrame()->childFrames()) {
        auto *table = qobject_cast<QTextTable *>(frame);
        if (!table)
            continue;
        if (table->cellAt(0, 0).firstCursorPosition().block().text() != QStringLiteral("quoted"))
            continue;
        const QTextTableCellFormat cellFormat = table->cellAt(0, 0).format().toTableCellFormat();
        QVERIFY(cellFormat.leftBorder() > 0.0);
        QVERIFY(cellFormat.leftPadding() > 0.0);
        found = true;
    }
    QVERIFY(found);
}

void MarkdownTest::linkSpan()
{
    const ThemeDefinition theme = ThemeDefinition::light();
    QTextDocument document;
    render(&document, QStringLiteral("[site](https://example.com)\n"));
    const QTextBlock block = document.firstBlock();
    const int at = int(block.text().indexOf(QStringLiteral("site")));

    bool found = false;
    for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (fragment.contains(at)) {
            const QTextCharFormat format = fragment.charFormat();
            QVERIFY(format.isAnchor());
            QCOMPARE(format.anchorHref(), QStringLiteral("https://example.com"));
            QCOMPARE(format.foreground().color(), theme.markdown.link);
            found = true;
        }
    }
    QVERIFY(found);
}

void MarkdownTest::strikethroughSpan()
{
    QTextDocument document;
    render(&document, QStringLiteral("~~gone~~\n"));
    const QTextBlock block = document.firstBlock();
    const int at = int(block.text().indexOf(QStringLiteral("gone")));
    QVERIFY(formatAt(block, at).fontStrikeOut());
}

void MarkdownTest::gfmTable()
{
    QTextDocument document;
    render(&document, QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 |\n"));
    QTextTable *table = nullptr;
    for (QTextFrame *frame : document.rootFrame()->childFrames()) {
        if (QTextTable *candidate = qobject_cast<QTextTable *>(frame)) {
            table = candidate;
            break;
        }
    }
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 2);
    QCOMPARE(table->columns(), 2);
    QCOMPARE(table->cellAt(0, 0).firstCursorPosition().block().text(), QStringLiteral("a"));
    QCOMPARE(table->cellAt(1, 1).firstCursorPosition().block().text(), QStringLiteral("2"));
}

void MarkdownTest::blocksRecordTheirSourceLine()
{
    // Click-to-source needs to know which line of the source a rendered block
    // came from; the builder is where that mapping can be produced, because it
    // is the only thing that sees the source and the blocks together.
    const QString markdown = QStringLiteral(
        "# Title\n"
        "\n"
        "first paragraph\n"
        "\n"
        "second paragraph\n"
        "\n"
        "```\n"
        "code line\n"
        "```\n"
        "\n"
        "- item\n");
    QTextDocument document;
    render(&document, markdown);

    const QVector<QPair<QString, int>> expected = {
        {QStringLiteral("Title"), 0},
        {QStringLiteral("first paragraph"), 2},
        {QStringLiteral("second paragraph"), 4},
        {QStringLiteral("code line"), 7},
        {QStringLiteral("item"), 10},
    };

    for (const auto &pair : expected) {
        QTextBlock block = document.firstBlock();
        while (block.isValid() && block.text() != pair.first)
            block = block.next();

        QVERIFY2(block.isValid(), qPrintable(QStringLiteral("no block %1").arg(pair.first)));
        const QVariant line = block.blockFormat().property(MarkdownBuilder::sourceLineKey);
        QVERIFY2(line.isValid(), qPrintable(QStringLiteral("block %1 has no source line").arg(pair.first)));
        QCOMPARE(line.toInt(), pair.second);
    }
}

void MarkdownTest::rulesLinksAndImagesMapToTheirLine()
{
    // These blocks have no literal text of their own to search for: a rule is
    // found by its shape, and a link or an image opens its block before any text
    // arrives. Each still has to land on its own line rather than the line of
    // whatever came before it.
    const QString markdown = QStringLiteral(
        "intro paragraph\n"
        "\n"
        "---\n"
        "\n"
        "[a link on its own](https://example.org)\n"
        "\n"
        "![missing image](does-not-exist.png)\n");
    QTextDocument document;
    render(&document, markdown);

    const auto lineOf = [&document](const std::function<bool(const QString &)> &matches) {
        for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
            if (!matches(block.text()))
                continue;
            const QVariant line = block.blockFormat().property(MarkdownBuilder::sourceLineKey);
            return line.isValid() ? line.toInt() : -999;
        }
        return -999;
    };

    QCOMPARE(lineOf([](const QString &text) {
                 return text.startsWith(QStringLiteral("intro"));
             }),
             0);
    QCOMPARE(lineOf([](const QString &text) {
                 return text.startsWith(QString(QChar(0x2500)));
             }),
             2);
    QCOMPARE(lineOf([](const QString &text) {
                 return text == QStringLiteral("a link on its own");
             }),
             4);
    QCOMPARE(lineOf([](const QString &text) {
                 return text.startsWith(QStringLiteral("[image:"));
             }),
             6);
}

void MarkdownTest::tableGridLinesUseTheBorderColor()
{
    QTextDocument document;
    render(&document, QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 |\n"));

    // QTextTableFormat::setBorderBrush() paints only the outer frame; the grid
    // between the cells is drawn from each cell's own border. Without one the
    // lines come out white, which the light theme cannot show.
    const QColor expected = ThemeDefinition::light().markdown.tableBorder;

    QTextTable *table = nullptr;
    for (QTextFrame *frame : document.rootFrame()->childFrames()) {
        if (auto *candidate = qobject_cast<QTextTable *>(frame)) {
            table = candidate;
            break;
        }
    }
    QVERIFY(table != nullptr);

    for (int row = 0; row < table->rows(); ++row) {
        for (int column = 0; column < table->columns(); ++column) {
            const QTextTableCellFormat format = table->cellAt(row, column).format().toTableCellFormat();
            QCOMPARE(format.topBorder(), 1.0);
            QCOMPARE(format.bottomBorder(), 1.0);
            QCOMPARE(format.leftBorder(), 1.0);
            QCOMPARE(format.rightBorder(), 1.0);
            for (const QBrush &brush : {format.topBorderBrush(), format.bottomBorderBrush(), format.leftBorderBrush(), format.rightBorderBrush()}) {
                QVERIFY2(brush.color() == expected,
                         qPrintable(QStringLiteral("cell %1,%2 border is %3, expected %4").arg(row).arg(column).arg(brush.color().name(), expected.name())));
            }
        }
    }
}

void MarkdownTest::imageResolvesAgainstBaseDir()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile png(dir.filePath(QStringLiteral("img.png")));
    QVERIFY(png.open(QIODevice::WriteOnly));
    png.write(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="));
    png.close();

    QTextDocument document;
    render(&document, QStringLiteral("![alt](img.png)\n"), dir.path());
    QTextImageFormat image;
    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.isImageFormat()) {
                image = format.toImageFormat();
                found = true;
            }
        }
    }
    QVERIFY(found);
    QCOMPARE(image.name(), QFileInfo(dir.filePath(QStringLiteral("img.png"))).absoluteFilePath());
}

void MarkdownTest::animatedImageBecomesObject()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile gif(dir.filePath(QStringLiteral("anim.gif")));
    QVERIFY(gif.open(QIODevice::WriteOnly));
    // Minimal single-frame GIF87a header; enough for suffix-based detection.
    gif.write(QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"));
    gif.close();

    QTextDocument document;
    render(&document, QStringLiteral("![alt](anim.gif)\n"), dir.path());
    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.objectType() == MarkdownBuilder::gifObjectType)
                found = true;
        }
    }
    QVERIFY(found);
}

void MarkdownTest::svgIsRasterisedWithAntialiasing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile svg(dir.filePath(QStringLiteral("circle.svg")));
    QVERIFY(svg.open(QIODevice::WriteOnly));
    svg.write(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"48\" height=\"48\">"
        "<circle cx=\"24\" cy=\"24\" r=\"18\" fill=\"#ff0000\"/></svg>");
    svg.close();

    QTextDocument document;
    render(&document, QStringLiteral("![circle](circle.svg)\n"), dir.path());

    QTextImageFormat image;
    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid() && !found; block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.isImageFormat()) {
                image = format.toImageFormat();
                found = true;
            }
        }
    }
    QVERIFY(found);
    // The SVG is handed over as a document resource.
    QVERIFY(image.name().startsWith(QStringLiteral("mmd-res-")));
    const QVariant resource = document.resource(QTextDocument::ImageResource, QUrl(image.name()));
    QVERIFY(resource.isValid());
    const QImage rasterised = resource.value<QImage>();
    QVERIFY(!rasterised.isNull());

    // Antialiasing leaves partially transparent pixels along the circle's edge;
    // without it every pixel would be fully opaque or fully transparent.
    int edgePixels = 0;
    for (int y = 0; y < rasterised.height(); ++y) {
        for (int x = 0; x < rasterised.width(); ++x) {
            const int alpha = qAlpha(rasterised.pixel(x, y));
            if (alpha > 0 && alpha < 255)
                ++edgePixels;
        }
    }
    QVERIFY(edgePixels > 0);
}

void MarkdownTest::horizontalRule()
{
    const ThemeDefinition theme = ThemeDefinition::light();
    QTextDocument document;
    render(&document, QStringLiteral("above\n\n---\n\nbelow\n"));
    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        if (block.blockFormat().intProperty(MarkdownBuilder::kindKey) == int(MarkdownBuilder::BlockKind::Rule)) {
            QVERIFY(!block.text().isEmpty());
            QCOMPARE(block.charFormat().foreground().color(), theme.markdown.rule);
            QCOMPARE(block.blockFormat().alignment(), Qt::AlignHCenter);
            found = true;
        }
    }
    QVERIFY(found);
}

void MarkdownTest::entityDecoding()
{
    QTextDocument document;
    render(&document, QStringLiteral("a &amp; b &copy; c\n"));
    const QString text = document.firstBlock().text();
    QVERIFY(text.contains(QStringLiteral("a & b")));
    QVERIFY(text.contains(QChar(0xa9)));
}

void MarkdownTest::softBreakStaysOneParagraph()
{
    QTextDocument document;
    render(&document, QStringLiteral("first line\nsecond line\n"));
    QCOMPARE(document.firstBlock().text(), QStringLiteral("first line second line"));
}

void MarkdownTest::emptyDocument()
{
    QTextDocument document;
    render(&document, QStringLiteral(""));
    QVERIFY(document.isEmpty());
}

void MarkdownTest::missingImageShowsPlaceholder()
{
    QTextDocument document;
    render(&document, QStringLiteral("![alt](missing.png)\n"), QStringLiteral("/nonexistent"));
    const QString all = document.toPlainText();
    QVERIFY(all.contains(QStringLiteral("missing.png")));
}

void MarkdownTest::htmlDivCentersContent()
{
    QTextDocument document;
    render(&document, QStringLiteral("<div align=\"center\">\n\n# Title\n\n</div>\n"));

    QTextBlock block;
    for (QTextBlock candidate = document.firstBlock(); candidate.isValid(); candidate = candidate.next()) {
        if (candidate.text() == QStringLiteral("Title"))
            block = candidate;
    }
    QVERIFY(block.isValid());
    QCOMPARE(block.blockFormat().alignment(), Qt::AlignHCenter);
}

void MarkdownTest::htmlSummaryIsRendered()
{
    QTextDocument document;
    render(&document, QStringLiteral("<details>\n<summary><strong>Table of contents</strong></summary>\n\n- one\n\n</details>\n"));

    const QString text = document.toPlainText();
    QVERIFY(text.contains(QStringLiteral("Table of contents")));
    QVERIFY(text.contains(QString(QChar(0x25B8)))); // the ▸ marker
    QVERIFY(!text.contains(QStringLiteral("<summary")));
}

void MarkdownTest::htmlInlineTagsAreApplied()
{
    QTextDocument document;
    render(&document, QStringLiteral("x <b>BOLD</b> y <code>CODE</code> z <sub>SUB</sub>\n"));

    const QTextBlock block = document.firstBlock();
    QVERIFY(block.isValid());
    QCOMPARE(formatAt(block, int(block.text().indexOf(QStringLiteral("BOLD")))).fontWeight(), int(QFont::Bold));
    QCOMPARE(formatAt(block, int(block.text().indexOf(QStringLiteral("CODE")))).background().color(), ThemeDefinition::light().markdown.codeBackground);
    QCOMPARE(formatAt(block, int(block.text().indexOf(QStringLiteral("SUB")))).verticalAlignment(), QTextCharFormat::AlignSubScript);
}

void MarkdownTest::htmlKbdAndRemoteImageBecomeChips()
{
    auto chipTexts = [](QTextDocument &doc) {
        QStringList out;
        for (QTextBlock block = doc.firstBlock(); block.isValid(); block = block.next()) {
            for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat format = it.fragment().charFormat();
                if (format.objectType() == MarkdownBuilder::chipObjectType)
                    out << format.property(MarkdownBuilder::chipTextKey).toString();
            }
        }
        return out;
    };

    QTextDocument keyboard;
    render(&keyboard, QStringLiteral("press <kbd>Ctrl</kbd> now\n"));
    QCOMPARE(chipTexts(keyboard), QStringList{QStringLiteral("Ctrl")});

    QTextDocument badge;
    render(&badge, QStringLiteral("x <img src=\"https://img.shields.io/badge.svg\" alt=\"build: passing\"> y\n"));
    QCOMPARE(chipTexts(badge), QStringList{QStringLiteral("build: passing")});
}

void MarkdownTest::calloutIsRendered()
{
    QTextDocument document;
    render(&document, QStringLiteral("> [!NOTE]\n> body text\n"));

    bool found = false;
    for (QTextFrame *frame : document.rootFrame()->childFrames()) {
        auto *table = qobject_cast<QTextTable *>(frame);
        if (!table)
            continue;
        const QTextTableCell cell = table->cellAt(0, 0);
        if (cell.firstCursorPosition().block().text() != QStringLiteral("Note"))
            continue;
        QCOMPARE(cell.format().toTableCellFormat().leftBorderBrush().color(), ThemeDefinition::light().markdown.link);
        found = true;
    }
    QVERIFY(found);
}

void MarkdownTest::headingSlugsMatchGithubStyle()
{
    QCOMPARE(MarkdownBuilder::headingSlug(QStringLiteral("Key features")), QStringLiteral("key-features"));
    QCOMPARE(MarkdownBuilder::headingSlug(QStringLiteral("Getting started")), QStringLiteral("getting-started"));
    QCOMPARE(MarkdownBuilder::headingSlug(QStringLiteral("Licensing rules (mandatory)")), QStringLiteral("licensing-rules-mandatory"));
    QCOMPARE(MarkdownBuilder::headingSlug(QStringLiteral("C++20 & Qt")), QStringLiteral("c20--qt"));
}

void MarkdownTest::htmlImageInsideCenteredDivIsCentered()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(40, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("pic.png"))));

    // The document starts with <div>, so the very first block must be centred
    // too (the image used to land in an unformatted first block).
    QTextDocument document;
    render(&document, QStringLiteral("<div align=\"center\">\n\n<img src=\"pic.png\" width=\"40\">\n\n</div>\n"), dir.path());

    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (!it.fragment().charFormat().isImageFormat())
                continue;
            QCOMPARE(block.blockFormat().alignment(), Qt::AlignHCenter);
            found = true;
        }
    }
    QVERIFY(found);
}

void MarkdownTest::htmlParagraphAlignCentersMarkdownBlocks()
{
    // <p align="center"> wraps markdown: CommonMark breaks the HTML block at
    // the blank line, so the heading arrives as an ordinary markdown block and
    // the alignment has to reach it.
    QTextDocument document;
    render(&document, QStringLiteral("<p align=\"center\">\n\n# Title\n\n</p>\n"));

    QTextBlock block;
    for (QTextBlock candidate = document.firstBlock(); candidate.isValid(); candidate = candidate.next()) {
        if (candidate.text() == QStringLiteral("Title"))
            block = candidate;
    }
    QVERIFY(block.isValid());
    QCOMPARE(block.blockFormat().alignment(), Qt::AlignHCenter);
}

void MarkdownTest::htmlTableCellsRenderTheirMarkdown()
{
    QTextDocument document;
    render(&document, QStringLiteral("<table><tr><td>**bold**</td><td>plain</td></tr></table>\n"));

    QTextTable *table = nullptr;
    for (QTextFrame::iterator it = document.rootFrame()->begin(); !it.atEnd(); ++it)
        if (auto *candidate = qobject_cast<QTextTable *>(it.currentFrame()))
            table = candidate;
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 1);
    QCOMPARE(table->columns(), 2);

    // The cell text keeps the markdown: the word comes through bold.
    const QTextBlock cellBlock = table->cellAt(0, 0).firstCursorPosition().block();
    QCOMPARE(cellBlock.text(), QStringLiteral("bold"));
    QVERIFY(formatAt(cellBlock, 0).fontWeight() >= QFont::Bold);
    QCOMPARE(table->cellAt(0, 1).firstCursorPosition().block().text(), QStringLiteral("plain"));
}

void MarkdownTest::htmlTableWithBlankLinesBecomesATable()
{
    // The NEXUS.md shape: a table whose cells contain markdown and blank lines,
    // which CommonMark would otherwise scatter across several HTML blocks.
    QTextDocument document;
    render(&document,
           QStringLiteral("<table>\n<tr>\n<td width=\"50%\">\n\n**left**\n\n- one\n- two\n\n</td>\n"
                          "<td width=\"50%\">\n\nright text\n\n</td>\n</tr>\n</table>\n"));

    QTextTable *table = nullptr;
    for (QTextFrame::iterator it = document.rootFrame()->begin(); !it.atEnd(); ++it)
        if (auto *candidate = qobject_cast<QTextTable *>(it.currentFrame()))
            table = candidate;
    QVERIFY(table != nullptr);
    QCOMPARE(table->rows(), 1);
    QCOMPARE(table->columns(), 2);

    const QTextBlock first = table->cellAt(0, 0).firstCursorPosition().block();
    QCOMPARE(first.text(), QStringLiteral("left"));
    QVERIFY(formatAt(first, 0).fontWeight() >= QFont::Bold);

    QTextCursor cell = table->cellAt(0, 0).firstCursorPosition();
    bool hasList = false;
    for (QTextBlock block = cell.block(); block.isValid(); block = block.next()) {
        if (block.textList())
            hasList = true;
    }
    QVERIFY(hasList);
}

void MarkdownTest::htmlTableImageIsConstrainedByCellWidth()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage testImg(1200, 600, QImage::Format_ARGB32);
    testImg.fill(Qt::red);
    QVERIFY(testImg.save(dir.filePath(QStringLiteral("big.png")), "PNG"));

    RenderStyle style;
    style.baseDir = dir.path();
    style.contentWidth = 600;
    style.maxContentWidth = 600;
    style.maxImageWidth = 600;

    QTextDocument document;
    MarkdownBuilder::build(&document,
                           QStringLiteral("<table><tr>"
                                          "<td width=\"50%\"><img src=\"big.png\" width=\"100%\"></td>"
                                          "<td width=\"50%\"><img src=\"big.png\" width=\"100%\"></td>"
                                          "</tr></table>\n"),
                           style);

    QVector<int> widths;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                widths.append(int(it.fragment().charFormat().toImageFormat().width()));
        }
    }

    QCOMPARE(widths.size(), 2);
    // In a 600px table with two 50% columns, each cell available width is ~300 - 18 = 282px.
    for (int w : widths) {
        QVERIFY(w <= 300);
        QVERIFY(w >= 250);
    }
}

void MarkdownTest::gfmTableImageIsConstrainedByCellWidth()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage testImg(1200, 600, QImage::Format_ARGB32);
    testImg.fill(Qt::green);
    QVERIFY(testImg.save(dir.filePath(QStringLiteral("big.png")), "PNG"));

    RenderStyle style;
    style.baseDir = dir.path();
    style.contentWidth = 600;
    style.maxContentWidth = 600;
    style.maxImageWidth = 600;

    QTextDocument document;
    MarkdownBuilder::build(&document,
                           QStringLiteral("| Left | Right |\n"
                                          "| --- | --- |\n"
                                          "| ![l](big.png) | ![r](big.png) |\n"),
                           style);

    QVector<int> widths;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                widths.append(int(it.fragment().charFormat().toImageFormat().width()));
        }
    }

    QCOMPARE(widths.size(), 2);
    for (int w : widths) {
        QVERIFY(w <= 300);
        QVERIFY(w >= 250);
    }
}

void MarkdownTest::htmlBadgeRowStaysOnOneLine()
{
    // A row of remote-image badges (each its own <img> line, as READMEs write
    // them) must come out as one line of chips, not one block per badge.
    QTextDocument document;
    render(&document,
           QStringLiteral("<p align=\"center\">\n\n"
                          "  <img src=\"https://example.com/a.svg\" alt=\"alpha\">\n"
                          "  <img src=\"https://example.com/b.svg\" alt=\"beta\">\n"
                          "  <img src=\"https://example.com/c.svg\" alt=\"gamma\">\n\n"
                          "</p>\n"));

    int chipBlocks = 0;
    int chips = 0;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        int inBlock = 0;
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().objectType() == MarkdownBuilder::chipObjectType)
                ++inBlock;
        }
        if (inBlock > 0) {
            ++chipBlocks;
            chips += inBlock;
        }
        if (inBlock > 0)
            QCOMPARE(block.blockFormat().alignment(), Qt::AlignHCenter);
    }
    QCOMPARE(chipBlocks, 1); // all three chips on one line
    QCOMPARE(chips, 3);
}

void MarkdownTest::htmlImageHonoursWidthAttribute()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(200, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("pic.png"))));

    QTextDocument document;
    render(&document, QStringLiteral("<img src=\"pic.png\" alt=\"x\" width=\"60\">\n"), dir.path());

    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid() && !found; block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (!format.isImageFormat())
                continue;
            const QTextImageFormat imageFormat = format.toImageFormat();
            QCOMPARE(int(imageFormat.width()), 60);
            QCOMPARE(int(imageFormat.height()), 12); // 200x40 scaled to width 60
            // Pre-scaled smoothly by the builder, so the paint is 1:1.
            const QVariant resource = document.resource(QTextDocument::ImageResource, QUrl(imageFormat.name()));
            QVERIFY(!resource.isNull());
            QCOMPARE(resource.value<QImage>().size(), QSize(60, 12));
            found = true;
        }
    }
    QVERIFY(found);
}

void MarkdownTest::htmlPercentWidthFillsTheContentColumn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(200, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("pic.png"))));

    // width="100%" asks for the whole text column, not the bag of digits "100".
    const int column = RenderStyle().maxContentWidth;

    QTextDocument document;
    render(&document, QStringLiteral("<img src=\"pic.png\" alt=\"x\" width=\"100%\">\n"), dir.path());

    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid() && !found; block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (!format.isImageFormat())
                continue;
            const QTextImageFormat imageFormat = format.toImageFormat();
            QCOMPARE(int(imageFormat.width()), column);
            QVERIFY(int(imageFormat.width()) != 100);
            QCOMPARE(int(imageFormat.height()), qRound(40.0 * column / 200.0));
            found = true;
        }
    }
    QVERIFY(found);

    // A percentage scales: half the column for width="50%".
    QTextDocument half;
    render(&half, QStringLiteral("<img src=\"pic.png\" alt=\"x\" width=\"50%\">\n"), dir.path());
    for (QTextBlock block = half.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (!it.fragment().charFormat().isImageFormat())
                continue;
            QCOMPARE(int(it.fragment().charFormat().toImageFormat().width()), column / 2);
        }
    }
}

void MarkdownTest::htmlPercentWidthOnRemoteImageIsSetBeforeLoad()
{
    // A remote banner must know its width before the download lands, so the
    // block has the right height from the first layout.
    RenderStyle style;
    style.theme = ThemeDefinition::light();
    style.remoteImagesEnabled = true;

    QTextDocument document;
    QVERIFY(MarkdownBuilder::build(&document, QStringLiteral("<img src=\"https://example.com/banner.svg\" alt=\"b\" width=\"100%\">\n"), style));

    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid() && !found; block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (!format.isImageFormat())
                continue;
            const QTextImageFormat imageFormat = format.toImageFormat();
            QCOMPARE(int(imageFormat.width()), style.maxContentWidth);
            found = true;
        }
    }
    QVERIFY(found);
}

void MarkdownTest::htmlWidthWithoutDigitsIsIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(200, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("pic.png"))));

    // No usable number: the image keeps its natural size, no explicit width.
    for (const QString &width : {QStringLiteral("abc"), QStringLiteral("%"), QStringLiteral("")}) {
        QTextDocument document;
        render(&document, QStringLiteral("<img src=\"pic.png\" alt=\"x\" width=\"%1\">\n").arg(width), dir.path());
        for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
            for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat format = it.fragment().charFormat();
                if (!format.isImageFormat())
                    continue;
                QCOMPARE(int(format.toImageFormat().width()), 0);
            }
        }
    }
}

void MarkdownTest::htmlPercentWidthFollowsTheLiveColumn()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(200, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("pic.png"))));

    // A narrow pane (the editor is open, the window is small) supplies the live
    // column; the percentage follows it so a hero does not overflow.
    RenderStyle style;
    style.theme = ThemeDefinition::light();
    style.baseDir = dir.path();
    style.maxContentWidth = 820;
    style.contentWidth = 640;

    QTextDocument document;
    QVERIFY(MarkdownBuilder::build(&document, QStringLiteral("<img src=\"pic.png\" alt=\"x\" width=\"100%\">\n"), style));

    bool found = false;
    for (QTextBlock block = document.firstBlock(); block.isValid() && !found; block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (!it.fragment().charFormat().isImageFormat())
                continue;
            QCOMPARE(int(it.fragment().charFormat().toImageFormat().width()), 640);
            found = true;
        }
    }
    QVERIFY(found);

    // An absolute width is unaffected by the column.
    QTextDocument fixed;
    style.contentWidth = 640;
    QVERIFY(MarkdownBuilder::build(&fixed, QStringLiteral("<img src=\"pic.png\" alt=\"x\" width=\"60\">\n"), style));
    for (QTextBlock block = fixed.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (!it.fragment().charFormat().isImageFormat())
                continue;
            QCOMPARE(int(it.fragment().charFormat().toImageFormat().width()), 60);
        }
    }
}

void MarkdownTest::htmlBlockImageIsNotStretchedToTheLineHeight()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(200, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("pic.png"))));

    QTextDocument document;
    render(&document, QStringLiteral("<img src=\"pic.png\" alt=\"x\" width=\"100%\">\n"), dir.path());
    const int column = RenderStyle().maxContentWidth;
    document.setTextWidth(column);

    QTextBlock block;
    for (QTextBlock candidate = document.firstBlock(); candidate.isValid(); candidate = candidate.next()) {
        for (QTextBlock::Iterator it = candidate.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                block = candidate;
        }
    }
    QVERIFY(block.isValid());
    // The prose line height (122%) must not stretch an image line: the block is
    // exactly the image's height, not 22% taller (which left a gap below it).
    const QRectF rect = document.documentLayout()->blockBoundingRect(block);
    QCOMPARE(qRound(rect.height()), qRound(40.0 * column / 200.0));
}

void MarkdownTest::standaloneBreakDoesNotMergeIntoTheNextImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(200, 40, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("pic.png"))));

    QTextDocument document;
    render(&document, QStringLiteral("<br>\n\n<img src=\"pic.png\" alt=\"x\" width=\"100%\">\n"), dir.path());

    QTextBlock imageBlock;
    for (QTextBlock candidate = document.firstBlock(); candidate.isValid(); candidate = candidate.next()) {
        for (QTextBlock::Iterator it = candidate.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                imageBlock = candidate;
        }
    }
    QVERIFY(imageBlock.isValid());
    // The <br> stays in its own block; it must not drag a line separator or a
    // stray space into the image block (which added an empty line above the
    // image and pushed a full-width image past the column).
    QVERIFY(!imageBlock.text().contains(QChar(0x2028)));
    QVERIFY(imageBlock.text().startsWith(QChar(QChar::ObjectReplacementCharacter)));
}

void MarkdownTest::imageInsideALinkIsClickable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(48, 20, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(dir.filePath(QStringLiteral("pic.png"))));

    // A badge wrapped in a link is the most common construct on GitHub profiles:
    // the image itself must carry the anchor, or hovering it never shows the
    // hand cursor and clicking it does nothing.
    QTextDocument markdownDocument;
    render(&markdownDocument, QStringLiteral("[![badge](pic.png)](https://example.com)\n"), dir.path());

    QTextImageFormat linkedImage;
    for (QTextBlock block = markdownDocument.firstBlock(); block.isValid() && linkedImage.name().isEmpty(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                linkedImage = it.fragment().charFormat().toImageFormat();
        }
    }
    QVERIFY(!linkedImage.name().isEmpty());
    QVERIFY(linkedImage.isAnchor());
    QCOMPARE(linkedImage.anchorHref(), QStringLiteral("https://example.com"));

    // The HTML spelling behaves the same.
    QTextDocument htmlDocument;
    render(&htmlDocument, QStringLiteral("<a href=\"https://example.com\"><img src=\"pic.png\" alt=\"badge\"></a>\n"), dir.path());

    QTextImageFormat htmlImage;
    for (QTextBlock block = htmlDocument.firstBlock(); block.isValid() && htmlImage.name().isEmpty(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().charFormat().isImageFormat())
                htmlImage = it.fragment().charFormat().toImageFormat();
        }
    }
    QVERIFY(htmlImage.isAnchor());
    QCOMPARE(htmlImage.anchorHref(), QStringLiteral("https://example.com"));
}

void MarkdownTest::readmeRendersWithoutLiteralHtml()
{
    QFile file(QStringLiteral(MMD_SOURCE_DIR "/README.md"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    QTextDocument document;
    render(&document, QString::fromUtf8(file.readAll()), QStringLiteral(MMD_SOURCE_DIR));

    const QString text = document.toPlainText();
    QVERIFY(text.contains(QStringLiteral("Total Markdown")));
    QVERIFY(text.contains(QStringLiteral("\u25B8 Table of contents")));
    // The hero's raw markup must be gone (the README documents the tags as
    // inline code, so check the actual markup, not the tag names).
    QVERIFY(!text.contains(QStringLiteral("<div")));
    QVERIFY(!text.contains(QStringLiteral("<summary><strong>")));
    QVERIFY(!text.contains(QStringLiteral("<img")));
}

QVector<MarkdownTest::FoundMath> MarkdownTest::mathObjectsIn(const QTextDocument *document)
{
    QVector<FoundMath> found;
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.charFormat().objectType() != MarkdownBuilder::mathObjectType)
                continue;

            FoundMath entry;
            entry.source = fragment.charFormat().stringProperty(MarkdownBuilder::mathSourceKey);
            entry.display = fragment.charFormat().property(MarkdownBuilder::mathDisplayKey).toBool();
            found.append(entry);
        }
    }
    return found;
}

void MarkdownTest::inlineMathBecomesAnObject()
{
    QTextDocument document;
    render(&document, QStringLiteral("Euler: $e^{i\\pi} + 1 = 0$ indeed\n"));

    const QVector<FoundMath> math = mathObjectsIn(&document);
    QCOMPARE(math.size(), 1);
    QCOMPARE(math.at(0).source, QStringLiteral("e^{i\\pi} + 1 = 0"));
    QCOMPARE(math.at(0).display, false);
}

void MarkdownTest::displayMathIsMarkedAndCentred()
{
    QTextDocument document;
    render(&document, QStringLiteral("Before\n\n$$\n\\int_0^1 x\\,dx\n$$\n\nAfter\n"));

    const QVector<FoundMath> math = mathObjectsIn(&document);
    QCOMPARE(math.size(), 1);
    QCOMPARE(math.at(0).source, QStringLiteral("\\int_0^1 x\\,dx"));
    QCOMPARE(math.at(0).display, true);

    // The formula stands in a block of its own, and that block is centred.
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        bool hasMath = false;
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            if (it.fragment().isValid() && it.fragment().charFormat().objectType() == MarkdownBuilder::mathObjectType)
                hasMath = true;
        }
        if (!hasMath)
            continue;
        QVERIFY2(block.blockFormat().alignment() & Qt::AlignHCenter, "the display-formula block is not centred");
        return;
    }
    QFAIL("no block carries the display formula");
}

void MarkdownTest::displayMathDoesNotStealTheNeighbourParagraph()
{
    // md4c glues a standalone $$...$$ to the paragraph before it: its lazy-math
    // rule swallows the blank line, so the display span arrives inside the text
    // paragraph. The builder must split it off -- the text keeps its own,
    // left-aligned block, and the formula gets a centred one.
    QTextDocument document;
    render(&document, QStringLiteral("Text line before.\n\n$$\nf(x)\n$$\n\nText line after.\n"));

    int index = 0;
    int mathBlock = -1;
    int textBlock = -1;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next(), ++index) {
        const bool hasMath = block.text().contains(QChar::ObjectReplacementCharacter);
        if (hasMath && mathBlock < 0)
            mathBlock = index;
        if (block.text().contains(QStringLiteral("Text line before.")))
            textBlock = index;
    }
    QVERIFY2(mathBlock >= 0 && mathBlock != textBlock, "the display formula was not split into its own block");

    const QTextBlock formula = [&document, mathBlock] {
        QTextBlock block = document.firstBlock();
        for (int i = 0; i < mathBlock; ++i)
            block = block.next();
        return block;
    }();
    QVERIFY2(formula.blockFormat().alignment() & Qt::AlignHCenter, "the formula block is not centred");

    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        if (block.text().contains(QStringLiteral("Text line before.")))
            QVERIFY2(!(block.blockFormat().alignment() & Qt::AlignHCenter), "the neighbour paragraph was centred instead");
        if (block.text().contains(QStringLiteral("Text line after.")))
            QVERIFY2(!(block.blockFormat().alignment() & Qt::AlignHCenter), "the trailing paragraph was centred instead");
    }
}

void MarkdownTest::escapedDollarStaysLiteral()
{
    QTextDocument document;
    render(&document, QStringLiteral("Costs \\$5 and \\$10\n"));

    QVERIFY(mathObjectsIn(&document).isEmpty());
    QCOMPARE(document.firstBlock().text(), QStringLiteral("Costs $5 and $10"));
}

void MarkdownTest::mathInsideCodeStaysLiteral()
{
    QTextDocument document;
    render(&document, QStringLiteral("Use `$x^2$` inline and\n\n```\n$a+b$\n```\n"));

    QVERIFY(mathObjectsIn(&document).isEmpty());
    QVERIFY(document.firstBlock().text().contains(QStringLiteral("$x^2$")));
    bool fenceHasDollars = false;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        if (block.blockFormat().intProperty(MarkdownBuilder::kindKey) == int(MarkdownBuilder::BlockKind::Code)
            && block.text().contains(QStringLiteral("$a+b$")))
            fenceHasDollars = true;
    }
    QVERIFY(fenceHasDollars);
}

void MarkdownTest::localImageCacheCachesAndInvalidatesOnMtime()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString filePath = tempDir.filePath(QStringLiteral("test.png"));

    QImage red(200, 100, QImage::Format_ARGB32);
    red.fill(Qt::red);
    QVERIFY(red.save(filePath));

    LocalImageCache::instance().clear();
    const QSize size1 = LocalImageCache::instance().naturalSize(filePath);
    QCOMPARE(size1, QSize(200, 100));

    const QImage scaled1 = LocalImageCache::instance().getScaled(filePath, QSize(100, 50), 1.0, size1);
    QCOMPARE(scaled1.size(), QSize(100, 50));
    QCOMPARE(scaled1.pixelColor(0, 0), QColor(Qt::red));

    // Overwrite with a blue image of different size
    // Sleep briefly so filesystem modification timestamp increments.
    QThread::msleep(50);
    QImage blue(300, 150, QImage::Format_ARGB32);
    blue.fill(Qt::blue);
    QVERIFY(blue.save(filePath));

    const QSize size2 = LocalImageCache::instance().naturalSize(filePath);
    QCOMPARE(size2, QSize(300, 150));

    const QImage scaled2 = LocalImageCache::instance().getScaled(filePath, QSize(150, 75), 1.0, size2);
    QCOMPARE(scaled2.size(), QSize(150, 75));
    QCOMPARE(scaled2.pixelColor(0, 0), QColor(Qt::blue));
}

void MarkdownTest::localImageCacheFastDownsampling()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString filePath = tempDir.filePath(QStringLiteral("large.png"));

    // 1000x1000 scaled to 100x100 is a 10x downsample (> 2.5x threshold for 2-step downsampling)
    QImage large(1000, 1000, QImage::Format_ARGB32);
    large.fill(Qt::green);
    QVERIFY(large.save(filePath));

    LocalImageCache::instance().clear();
    const QSize natural = LocalImageCache::instance().naturalSize(filePath);
    QCOMPARE(natural, QSize(1000, 1000));

    const QImage scaled = LocalImageCache::instance().getScaled(filePath, QSize(100, 100), 2.0, natural);
    QCOMPARE(scaled.size(), QSize(100, 100));
    QCOMPARE(scaled.devicePixelRatio(), 2.0);
    QCOMPARE(scaled.pixelColor(50, 50), QColor(Qt::green));
}

void MarkdownTest::repeatedRendersReuseCachedImages()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString filePath = tempDir.filePath(QStringLiteral("sample.png"));

    QImage img(400, 200, QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    QVERIFY(img.save(filePath));

    LocalImageCache::instance().clear();
    const QString md = QStringLiteral("![Sample](sample.png)\n");

    QTextDocument doc1;
    render(&doc1, md, tempDir.path());

    QTextDocument doc2;
    render(&doc2, md, tempDir.path());

    // Both documents should successfully contain the rendered markdown block
    QVERIFY(!doc1.toPlainText().isEmpty());
    QVERIFY(!doc2.toPlainText().isEmpty());
}

QTEST_MAIN(MarkdownTest)
#include "test_markdown.moc"
