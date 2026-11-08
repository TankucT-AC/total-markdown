// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/MarkdownBuilder.h"

#include "core/HtmlFragment.h"
#include "core/ImageResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QStack>
#include <QSvgRenderer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QTextTableCell>
#include <QUrl>

extern "C" {
#include "entity.h"
#include "md4c.h"
}

#include <memory>
#include <optional>
#include <vector>

#include <algorithm>

namespace
{

constexpr unsigned kParserFlags =
    MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_COLLAPSEWHITESPACE | MD_FLAG_LATEXMATHSPANS;

constexpr int kRuleLength = 40;

// Defined further down; the parser entry point needs it earlier.
static QString extractHtmlTables(const QString &source, QVector<QString> &tables);

QString decodeEntityText(const char *data, unsigned size)
{
    // The entity table keys include the '&' and ';' (e.g. "&amp;").
    const ENTITY *entity = entity_lookup(data, size_t(size));
    if (!entity)
        return QString::fromUtf8(data, int(size));

    const char32_t codepoints[2] = {entity->codepoints[0], entity->codepoints[1]};
    return QString::fromUcs4(codepoints, entity->codepoints[1] != 0 ? 2 : 1);
}

// MD_ATTRIBUTE strings arrive as a chain of typed substrings; entities inside
// them are not decoded by md4c itself.
QString decodeAttribute(const MD_ATTRIBUTE &attribute)
{
    QString result;
    if (attribute.size == 0)
        return result;

    for (unsigned i = 0;; ++i) {
        const unsigned begin = attribute.substr_offsets[i];
        const unsigned end = attribute.substr_offsets[i + 1];
        switch (attribute.substr_types[i]) {
        case MD_TEXT_ENTITY:
            result += decodeEntityText(attribute.text + begin, end - begin);
            break;
        case MD_TEXT_NULLCHAR:
            result += QChar::ReplacementCharacter;
            break;
        default:
            result += QString::fromUtf8(attribute.text + begin, int(end - begin));
            break;
        }
        if (end == attribute.size)
            break;
    }
    return result;
}

qreal devicePixelRatio()
{
    if (const QScreen *screen = QGuiApplication::primaryScreen())
        return screen->devicePixelRatio();
    return 1.0;
}

// An HTML length attribute carries either an absolute pixel count or a
// percentage of the text column. The two must not be confused: "100%" used to
// be read as the digits "100" and drew a full-width banner at 100 px.
struct HtmlLength {
    int pixels = 0;
    int percent = 0;
};

HtmlLength parseHtmlLength(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return {};

    bool ok = false;
    if (const int percentSign = int(trimmed.indexOf(QLatin1Char('%'))); percentSign >= 0) {
        const int percent = trimmed.left(percentSign).trimmed().toInt(&ok);
        return ok && percent > 0 ? HtmlLength{0, percent} : HtmlLength{};
    }

    const QString digits = QString(trimmed).remove(QRegularExpression(QStringLiteral("[^0-9]")));
    if (digits.isEmpty())
        return {};
    const int pixels = digits.toInt(&ok);
    return ok && pixels > 0 ? HtmlLength{pixels, 0} : HtmlLength{};
}

// Rasterises an SVG at the requested pixel size with antialiasing, so scaled
// vector art stays smooth instead of showing stair-stepped edges.
QImage rasteriseSvg(const QString &path, const QSize &pixelSize)
{
    QSvgRenderer renderer(path);
    if (!renderer.isValid() || !pixelSize.isValid())
        return QImage();

    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(QPointF(0.0, 0.0), QSizeF(pixelSize)));
    painter.end();
    return image;
}

class Builder
{
public:
    Builder(QTextDocument *document, const RenderStyle &style)
        : m_document(document)
        , m_style(style)
        , m_cursor(document)
        , m_bodyFont(style.fonts.bodyFont())
        , m_codeFont(style.fonts.codeFont())
    {
        for (int level = 1; level <= 6; ++level)
            m_headingFonts[level] = style.fonts.headingFont(level);
    }

    bool run(const QString &markdown);

    int enterBlock(MD_BLOCKTYPE type, void *detail);
    int leaveBlock(MD_BLOCKTYPE type, void *detail);
    int enterSpan(MD_SPANTYPE type, void *detail);
    int leaveSpan(MD_SPANTYPE type, void *detail);
    int text(MD_TEXTTYPE type, const char *data, unsigned size);

private:
    struct SpanFormatState {
        bool em = false;
        bool strong = false;
        bool del = false;
        bool code = false;
        int sub = 0;
        int sup = 0;
        bool kbd = false;
    };

    struct PendingBlock {
        bool valid = false;
        QTextBlockFormat format;
        QTextCharFormat baseCharFormat;
        bool attachToList = false;
        int sourceLine = -1;
    };

    struct Cell {
        std::unique_ptr<QTextDocument> document;
        bool header = false;
        Qt::Alignment alignment = Qt::AlignLeft;
    };

    struct TableState {
        unsigned columns = 0;
        unsigned headerRows = 0;
        bool inHead = false;
        std::vector<Cell> currentRow;
        std::vector<std::vector<Cell>> rows;
    };

    struct SavedState {
        QTextCursor cursor;
        PendingBlock pending;
        QTextCharFormat blockCharFormat;
        SpanFormatState spans;
        QString linkHref;
        bool firstBlock = true;
        RenderStyle style;
    };

    struct FrameContext {
        QTextFrame *frame = nullptr;
        QTextCursor outerCursor;
        QTextTableCellFormat quoteCellFormat; // re-applied on close (see popFrame)
        bool isQuote = false;
    };

    void setPendingBlock(const QTextBlockFormat &format, const QTextCharFormat &base, bool attachToList);
    void startBlock();
    // Source-line mapping. md4c hands text over in document order, so a cursor
    // advanced over the source as the text arrives stays in step with the
    // blocks; used to send a click in the preview back to the editor's line.
    int consumeSource(const QString &text);
    // Marks the block under the cursor with the line it came from, unless it
    // already knows: a span (link, image, formula) can open a block before its
    // text arrives, so the first literal run is what identifies the line.
    void stampCurrentBlock(int line);
    // Line of the next horizontal rule in the source. A rule carries no text of
    // its own, so it is found by shape instead of by its content.
    int lineOfNextRule();
    void insertImageElement(const QString &source, const QString &alt, HtmlLength width = {});
    void insertMathElement(const QString &tex, bool display);
    void insertChip(const QString &text, MarkdownBuilder::ChipKind kind);
    void insertVerbatim(const QString &content, int sourceLine);
    void startVerbatimFrame(const QColor &background, const QString &language);
    void endVerbatimFrame();
    void removeFrameSpacers();
    void emitTable();

    // HTML support: a tolerant subset that makes GitHub READMEs readable.
    void startHtmlBlock();
    void renderHtmlBlock(const QString &html);
    void renderHtmlFragment(const QString &html, const QVector<HtmlToken> &tokens, int begin, int end);
    void renderHtmlContainer(const HtmlToken &token, const QString &innerHtml);
    void renderHtmlSubdocument(const QString &innerHtml, Qt::Alignment alignment);
    void insertHtmlSubdocument(const QTextDocument &source, Qt::Alignment alignment);
    void copyHtmlResources(const QTextDocument &source);
    void renderHtmlTable(const QString &innerHtml);
    void applyInlineHtml(const QString &html, bool openBlock = false);
    void insertSummary(const QString &html);
    void renderCallout(QTextFrame *quote);

    QString linkFor(const QString &href) const;

    QTextFrame *pushFrame(const QTextFrameFormat &format);
    QTextFrame *pushQuote();
    void popFrame();

    QTextCharFormat composeCharFormat() const;
    QTextCharFormat bodyCharFormat() const;
    QTextCharFormat codeCharFormat() const;
    QTextCharFormat headingCharFormat(int level) const;
    QTextCharFormat ruleCharFormat() const;

    QTextBlockFormat paragraphFormat() const;
    // Cell format for tables: Qt draws the grid between cells from the cell's
    // own border, so the colour has to live there, not only on the table.
    QTextTableCellFormat tableCellFormat(bool header) const;
    QTextBlockFormat headingFormat(int level) const;
    QTextBlockFormat codeFormat(const QString &language) const;
    QTextFrameFormat codeFrameFormat(const QColor &background) const;
    QTextBlockFormat ruleFormat() const;

    QTextDocument *m_document = nullptr;
    RenderStyle m_style;
    QTextCursor m_cursor;

    // Resolved once: building a QFont per block/span would repeat
    // QFontDatabase lookups for the system font set.
    QFont m_bodyFont;
    QFont m_codeFont;
    QFont m_headingFonts[7];

    bool m_firstBlock = true;
    PendingBlock m_pending;
    QTextCharFormat m_blockCharFormat;

    // The source being built, plus the cursor used to map blocks back to lines.
    QString m_source;
    // Whole HTML tables lifted out of the source before md4c runs, indexed by
    // the marker that stands in their place.
    QVector<QString> m_extractedTables;
    qsizetype m_sourceScan = 0;
    int m_sourceLine = 0;
    // Start line of the last literal run consumed; the line a formula or an
    // image that arrives after its own text belongs to.
    int m_lastRunLine = 0;
    // Source line of the first line of the code block being collected.
    int m_verbatimSourceLine = -1;

    SpanFormatState m_span;
    QString m_linkHref;

    bool m_taskDone = false;
    bool m_taskPending = false;
    int m_imageDepth = 0;

    // LaTeX math span collection: the TeX source arrives as raw text callbacks
    // between the span's enter and leave, the way an image collects its alt text.
    int m_mathDepth = 0;
    bool m_mathDisplay = false;
    QString m_mathBuffer;

    bool m_inListItem = false;
    bool m_listItemFirstBlockDone = false;
    QVector<QTextListFormat> m_pendingListFormats;
    QVector<QTextList *> m_lists;
    QVector<FrameContext> m_frameStack;

    bool m_inVerbatimBlock = false;
    bool m_verbatimPendingNewline = false;
    QTextBlockFormat m_verbatimBlockFormat;
    QTextFrame *m_verbatimFrame = nullptr;
    QTextCursor m_verbatimCursor;

    std::optional<TableState> m_table = std::nullopt;
    QStack<SavedState> m_cellStack;

    // HTML subset state.
    bool m_inHtmlBlock = false;
    QString m_htmlBuffer;
    QString m_pendingImageSrc;
    QString m_imageAltBuffer;
    QVector<QPair<QString, Qt::Alignment>> m_openHtmlBlocks; // containers spanning blocks
    // True while the block under the cursor is a run of inline HTML (a row of
    // badges); the next inline HTML block continues it instead of starting a new one.
    bool m_htmlInlineContinued = false;
    int m_htmlDetailsDepth = 0; // inside <details>
    QVector<QString> m_htmlTagStack; // open inline tags, for matching </...>
    QString m_kbdBuffer; // text collected between <kbd> and </kbd>
    bool m_inKbd = false;
};

int onEnterBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    return static_cast<Builder *>(userdata)->enterBlock(type, detail);
}

int onLeaveBlock(MD_BLOCKTYPE type, void *detail, void *userdata)
{
    return static_cast<Builder *>(userdata)->leaveBlock(type, detail);
}

int onEnterSpan(MD_SPANTYPE type, void *detail, void *userdata)
{
    return static_cast<Builder *>(userdata)->enterSpan(type, detail);
}

int onLeaveSpan(MD_SPANTYPE type, void *detail, void *userdata)
{
    return static_cast<Builder *>(userdata)->leaveSpan(type, detail);
}

int onText(MD_TEXTTYPE type, const char *data, MD_SIZE size, void *userdata)
{
    return static_cast<Builder *>(userdata)->text(type, data, size);
}

bool Builder::run(const QString &markdown)
{
    // Pull whole HTML tables out first (see extractHtmlTables); the extracted
    // source keeps the line count, so source-line mapping is unaffected.
    m_extractedTables.clear();
    m_source = extractHtmlTables(markdown, m_extractedTables);
    const QByteArray utf8 = m_source.toUtf8();

    m_sourceScan = 0;
    m_sourceLine = 0;
    m_verbatimSourceLine = -1;

    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = kParserFlags;
    parser.enter_block = &onEnterBlock;
    parser.leave_block = &onLeaveBlock;
    parser.enter_span = &onEnterSpan;
    parser.leave_span = &onLeaveSpan;
    parser.text = &onText;

    m_document->clear();
    m_document->setIndentWidth(24); // ~2 spaces per list level
    m_cursor = QTextCursor(m_document);
    m_firstBlock = true;
    m_pending = PendingBlock{};

    const bool ok = md_parse(utf8.constData(), MD_SIZE(utf8.size()), &parser, this) == 0;
    removeFrameSpacers();
    return ok;
}

void Builder::setPendingBlock(const QTextBlockFormat &format, const QTextCharFormat &base, bool attachToList)
{
    // The line is filled in by the block's first literal text run; a block that
    // never gets one (an empty list item, say) simply stays unmapped.
    m_pending = PendingBlock{true, format, base, attachToList, -1};
}

int Builder::consumeSource(const QString &text)
{
    if (text.isEmpty())
        return m_sourceLine;

    // Entities, escapes and collapsed whitespace mean a run is not always in
    // the source verbatim. A miss leaves the cursor alone, and the next literal
    // run picks the line count back up.
    const qsizetype found = m_source.indexOf(text, m_sourceScan);
    if (found < 0)
        return m_sourceLine;

    int line = m_sourceLine;
    for (qsizetype i = m_sourceScan; i < found; ++i) {
        if (m_source.at(i) == QLatin1Char('\n'))
            ++line;
    }
    const int startLine = line;

    // A run can span lines (code blocks, hard wraps), so the chunk itself has
    // to be counted too, or every later block drifts upwards.
    for (qsizetype i = found; i < found + text.size(); ++i) {
        if (m_source.at(i) == QLatin1Char('\n'))
            ++line;
    }

    m_sourceLine = line;
    m_lastRunLine = startLine;
    m_sourceScan = found + text.size();
    return startLine;
}

void Builder::stampCurrentBlock(int line)
{
    if (line < 0)
        return;

    const QTextBlock block = m_cursor.block();
    if (!block.isValid() || block.blockFormat().hasProperty(MarkdownBuilder::sourceLineKey))
        return;

    QTextBlockFormat format = block.blockFormat();
    format.setProperty(MarkdownBuilder::sourceLineKey, line);
    m_cursor.setBlockFormat(format);
}

int Builder::lineOfNextRule()
{
    qsizetype start = m_sourceScan;
    int line = m_sourceLine;
    while (start <= m_source.size()) {
        const qsizetype newline = m_source.indexOf(QLatin1Char('\n'), start);
        const QStringView text = QStringView(m_source).mid(start, newline < 0 ? -1 : newline - start);
        const QStringView trimmed = text.trimmed();
        if (trimmed.size() >= 3) {
            const QChar marker = trimmed.at(0);
            const bool repeated = std::all_of(trimmed.cbegin(), trimmed.cend(), [marker](QChar character) {
                return character == marker;
            });
            if (repeated && (marker == QLatin1Char('-') || marker == QLatin1Char('*') || marker == QLatin1Char('_'))) {
                m_sourceScan = start;
                m_sourceLine = line;
                return line;
            }
        }
        if (newline < 0)
            break;
        start = newline + 1;
        ++line;
    }
    return m_sourceLine;
}

void Builder::startBlock()
{
    if (!m_pending.valid)
        return;
    const PendingBlock pending = m_pending;
    m_pending = PendingBlock{};
    m_blockCharFormat = pending.baseCharFormat;
    m_htmlInlineContinued = false;

    QTextBlockFormat blockFormat = pending.format;
    if (pending.sourceLine >= 0)
        blockFormat.setProperty(MarkdownBuilder::sourceLineKey, pending.sourceLine);
    // A block-level HTML container (<div align="center">, <p align="right">)
    // can span several markdown blocks -- CommonMark breaks the HTML block at
    // a blank line -- so its alignment is carried on a stack and applied to
    // every block until the closing tag arrives.
    if (!m_openHtmlBlocks.isEmpty()) {
        const Qt::Alignment alignment = m_openHtmlBlocks.last().second;
        if (alignment != Qt::Alignment())
            blockFormat.setAlignment(alignment);
    }

    if (m_firstBlock) {
        m_cursor.setBlockFormat(blockFormat);
        m_cursor.setBlockCharFormat(pending.baseCharFormat);
        m_firstBlock = false;
    } else {
        m_cursor.insertBlock(blockFormat, pending.baseCharFormat);
    }

    if (pending.attachToList) {
        if (!m_pendingListFormats.isEmpty()) {
            QTextListFormat listFormat = m_pendingListFormats.takeLast();
            // Task lists show the checkbox instead of a bullet (Zed/GitHub style).
            if (m_taskPending)
                listFormat.setStyle(QTextListFormat::ListStyleUndefined);
            listFormat.setIndent(int(m_lists.size()) + 1);
            m_lists.append(m_cursor.createList(listFormat));
        } else if (!m_lists.isEmpty() && m_lists.last()->itemNumber(m_cursor.block()) < 0) {
            m_lists.last()->add(m_cursor.block());
        }
        if (m_inListItem)
            m_listItemFirstBlockDone = true;
    } else if (!m_lists.isEmpty() && m_inListItem && m_listItemFirstBlockDone) {
        // Continuation paragraph inside a list item: align with the markers.
        QTextBlockFormat adjusted = m_cursor.block().blockFormat();
        adjusted.setIndent(int(m_lists.size()));
        m_cursor.setBlockFormat(adjusted);
    }

    if (m_taskPending) {
        QTextCharFormat markerFormat = pending.baseCharFormat;
        const QFontMetricsF metrics(markerFormat.font());
        const qreal side = qRound(metrics.height() * 0.7);
        markerFormat.setObjectType(MarkdownBuilder::checkboxObjectType);
        markerFormat.setProperty(MarkdownBuilder::checkboxCheckedKey, m_taskDone);
        markerFormat.setProperty(MarkdownBuilder::checkboxSizeKey, side);
        m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), markerFormat);
        m_cursor.insertText(QStringLiteral("  "), pending.baseCharFormat);
        m_taskPending = false;
    }
}

QTextCharFormat Builder::composeCharFormat() const
{
    QTextCharFormat format = m_blockCharFormat;
    if (m_span.strong)
        format.setFontWeight(QFont::Bold);
    if (m_span.em)
        format.setFontItalic(true);
    if (m_span.del)
        format.setFontStrikeOut(true);
    if (m_span.code) {
        format.setFont(m_style.fonts.codeFont());
        format.setBackground(m_style.theme.markdown.codeBackground);
        format.setForeground(m_style.theme.markdown.codeText);
    }
    if (m_span.kbd) {
        QFont key = m_style.fonts.codeFont();
        key.setPointSizeF(key.pointSizeF() * 0.92);
        format.setFont(key);
        format.setBackground(m_style.theme.markdown.codeBackground);
        format.setForeground(m_style.theme.markdown.codeText);
    }
    if (m_span.sup > 0)
        format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    else if (m_span.sub > 0)
        format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    if (!m_linkHref.isEmpty()) {
        format.setAnchor(true);
        format.setAnchorHref(m_linkHref);
        format.setForeground(m_style.theme.markdown.link);
        format.setFontUnderline(true);
    }
    return format;
}

QTextCharFormat Builder::bodyCharFormat() const
{
    QTextCharFormat format;
    format.setFont(m_bodyFont);
    format.setForeground(m_style.theme.palette.color(QPalette::Text));
    return format;
}

QTextCharFormat Builder::codeCharFormat() const
{
    QTextCharFormat format;
    format.setFont(m_codeFont);
    format.setForeground(m_style.theme.markdown.codeText);
    return format;
}

QTextCharFormat Builder::headingCharFormat(int level) const
{
    QTextCharFormat format;
    format.setFont(m_headingFonts[std::clamp(level, 1, 6)]);
    format.setForeground(level >= 6 ? m_style.theme.markdown.weakText : m_style.theme.markdown.heading);
    return format;
}

QTextCharFormat Builder::ruleCharFormat() const
{
    QTextCharFormat format;
    format.setFont(m_bodyFont);
    format.setForeground(m_style.theme.markdown.rule);
    return format;
}

QTextBlockFormat Builder::paragraphFormat() const
{
    QTextBlockFormat format;
    format.setLineHeight(122, QTextBlockFormat::ProportionalHeight);
    format.setBottomMargin(3);
    return format;
}

QTextBlockFormat Builder::headingFormat(int level) const
{
    QTextBlockFormat format;
    switch (level) {
    case 1:
        format.setTopMargin(14);
        format.setBottomMargin(5);
        break;
    case 2:
        format.setTopMargin(12);
        format.setBottomMargin(4);
        break;
    case 3:
        format.setTopMargin(10);
        format.setBottomMargin(3);
        break;
    case 6:
        format.setTopMargin(8);
        format.setBottomMargin(2);
        break;
    default:
        format.setTopMargin(8);
        format.setBottomMargin(2);
        break;
    }
    format.setProperty(MarkdownBuilder::kindKey, int(MarkdownBuilder::BlockKind::Heading));
    format.setProperty(MarkdownBuilder::headingLevelKey, level);
    return format;
}

QTextBlockFormat Builder::codeFormat(const QString &language) const
{
    // The shaded box is a QTextFrame wrapping the whole fenced region, so the
    // lines themselves carry no background, padding or outer spacing.
    QTextBlockFormat format;
    format.setTopMargin(0);
    format.setBottomMargin(0);
    format.setLeftMargin(0);
    format.setRightMargin(0);
    format.setProperty(MarkdownBuilder::kindKey, int(MarkdownBuilder::BlockKind::Code));
    format.setProperty(MarkdownBuilder::languageKey, language);
    return format;
}

QTextFrameFormat Builder::codeFrameFormat(const QColor &background) const
{
    QTextFrameFormat format;
    format.setBackground(background);
    format.setBorder(0);
    format.setPadding(8);
    format.setTopMargin(6);
    format.setBottomMargin(6);
    // Tag the frame so the view can find code boxes (e.g. for the copy button).
    format.setProperty(MarkdownBuilder::kindKey, int(MarkdownBuilder::BlockKind::Code));
    return format;
}

QTextBlockFormat Builder::ruleFormat() const
{
    QTextBlockFormat format;
    format.setAlignment(Qt::AlignHCenter);
    format.setTopMargin(6);
    format.setBottomMargin(6);
    format.setProperty(MarkdownBuilder::kindKey, int(MarkdownBuilder::BlockKind::Rule));
    return format;
}

QTextTableCellFormat Builder::tableCellFormat(bool header) const
{
    QTextTableCellFormat format;
    format.setBorder(1);
    format.setBorderBrush(QBrush(m_style.theme.markdown.tableBorder));
    format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    if (header) {
        format.setBackground(m_style.theme.markdown.tableHeaderBackground);
        format.setFontWeight(QFont::Bold);
    }
    return format;
}

QTextFrame *Builder::pushFrame(const QTextFrameFormat &format)
{
    FrameContext context;
    context.outerCursor = m_cursor;
    context.frame = m_cursor.insertFrame(format);
    m_firstBlock = false;
    m_frameStack.append(context);
    m_cursor = context.frame->firstCursorPosition();
    return context.frame;
}

QTextFrame *Builder::pushQuote()
{
    // GitHub-style quote: an indent plus a 4px bar on the left, no fill. Only a
    // table cell supports a single-edge border, so the quote is a 1x1 table.
    QTextTableFormat tableFormat;
    tableFormat.setBorder(0);
    tableFormat.setCellSpacing(0);
    tableFormat.setCellPadding(0);
    tableFormat.setMargin(0);
    QVector<QTextLength> constraints;
    constraints.append(QTextLength(QTextLength::PercentageLength, 100));
    tableFormat.setColumnWidthConstraints(constraints);

    FrameContext context;
    context.outerCursor = m_cursor;
    QTextTable *table = m_cursor.insertTable(1, 1, tableFormat);

    QTextTableCellFormat cellFormat;
    cellFormat.setLeftBorder(4);
    cellFormat.setLeftBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    cellFormat.setLeftBorderBrush(m_style.theme.markdown.quoteBar);
    cellFormat.setLeftPadding(14);
    cellFormat.setTopPadding(2);
    cellFormat.setBottomPadding(2);
    table->cellAt(0, 0).setFormat(cellFormat);

    context.frame = table;
    context.quoteCellFormat = cellFormat;
    context.isQuote = true;
    m_frameStack.append(context);
    m_cursor = table->cellAt(0, 0).firstCursorPosition();
    // Reuse the cell's existing empty block as the first quote line.
    m_firstBlock = true;
    return table;
}

void Builder::popFrame()
{
    if (m_frameStack.isEmpty())
        return;
    const FrameContext context = m_frameStack.takeLast();
    // Cell formats are wiped when the cell's blocks are formatted, so the
    // quote bar is applied last, once the content is in place.
    if (context.isQuote) {
        if (auto *table = qobject_cast<QTextTable *>(context.frame))
            table->cellAt(0, 0).setFormat(context.quoteCellFormat);
        renderCallout(context.frame);
    }
    m_cursor = QTextCursor(m_document);
    m_cursor.setPosition(context.frame->lastPosition() + 1);
    m_firstBlock = false;
}

QString Builder::linkFor(const QString &href) const
{
    if (href.isEmpty())
        return QString();

    // In-document anchors stay fragments; the view handles them itself.
    if (href.startsWith(QLatin1Char('#')))
        return href;

    const QUrl url(href);
    if (!url.scheme().isEmpty())
        return href;
    if (m_style.baseDir.isEmpty())
        return href;
    return QUrl::fromLocalFile(QDir(m_style.baseDir).filePath(href)).toString();
}

int Builder::enterBlock(MD_BLOCKTYPE type, void *detail)
{
    switch (type) {
    case MD_BLOCK_DOC:
        break;
    case MD_BLOCK_QUOTE:
        pushQuote();
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL: {
        QTextListFormat format;
        format.setStyle(type == MD_BLOCK_OL ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
        m_pendingListFormats.append(format);
        break;
    }
    case MD_BLOCK_LI: {
        const auto *item = static_cast<const MD_BLOCK_LI_DETAIL *>(detail);
        m_inListItem = true;
        m_listItemFirstBlockDone = false;
        m_taskDone = false;
        m_taskPending = false;
        if (item && item->is_task) {
            m_taskPending = true;
            m_taskDone = item->task_mark != ' ';
        }
        setPendingBlock(paragraphFormat(), bodyCharFormat(), true);
        break;
    }
    case MD_BLOCK_H: {
        const auto *heading = static_cast<const MD_BLOCK_H_DETAIL *>(detail);
        const int level = int(heading->level);
        setPendingBlock(headingFormat(level), headingCharFormat(level), false);
        break;
    }
    case MD_BLOCK_CODE: {
        const auto *code = static_cast<const MD_BLOCK_CODE_DETAIL *>(detail);
        startVerbatimFrame(m_style.theme.markdown.codeBackground, decodeAttribute(code->lang));
        break;
    }
    case MD_BLOCK_HTML:
        m_inHtmlBlock = true;
        m_htmlBuffer.clear();
        break;
    case MD_BLOCK_P:
        setPendingBlock(paragraphFormat(), bodyCharFormat(), m_inListItem && !m_listItemFirstBlockDone);
        break;
    case MD_BLOCK_HR:
        setPendingBlock(ruleFormat(), ruleCharFormat(), false);
        break;
    case MD_BLOCK_TABLE: {
        const auto *table = static_cast<const MD_BLOCK_TABLE_DETAIL *>(detail);
        m_table = TableState();
        m_table->columns = table->col_count;
        break;
    }
    case MD_BLOCK_THEAD:
        if (m_table)
            m_table->inHead = true;
        break;
    case MD_BLOCK_TBODY:
        if (m_table)
            m_table->inHead = false;
        break;
    case MD_BLOCK_TR:
        if (m_table)
            m_table->currentRow.clear();
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        if (!m_table)
            break;
        const auto *cellDetail = static_cast<const MD_BLOCK_TD_DETAIL *>(detail);

        SavedState saved;
        saved.cursor = m_cursor;
        saved.pending = m_pending;
        saved.blockCharFormat = m_blockCharFormat;
        saved.spans = m_span;
        saved.linkHref = m_linkHref;
        saved.firstBlock = m_firstBlock;
        saved.style = m_style;
        m_cellStack.push(saved);

        Cell cell;
        cell.document = std::make_unique<QTextDocument>();
        cell.header = m_table->inHead;
        switch (cellDetail->align) {
        case MD_ALIGN_CENTER:
            cell.alignment = Qt::AlignHCenter;
            break;
        case MD_ALIGN_RIGHT:
            cell.alignment = Qt::AlignRight;
            break;
        default:
            cell.alignment = Qt::AlignLeft;
            break;
        }
        m_table->currentRow.push_back(std::move(cell));

        m_cursor = QTextCursor(m_table->currentRow.back().document.get());
        m_firstBlock = true;
        m_span = SpanFormatState{};
        m_linkHref.clear();

        const int totalTableWidth = m_style.contentWidth > 0 ? m_style.contentWidth : m_style.maxContentWidth;
        const unsigned cols = (std::max)(1u, m_table->columns);
        const int cellAvailableWidth = (std::max)(40, qRound(qreal(totalTableWidth) / qreal(cols)) - (8 * 2 + 2));
        m_style.contentWidth = cellAvailableWidth;
        m_style.maxContentWidth = cellAvailableWidth;
        m_style.maxImageWidth = cellAvailableWidth;

        setPendingBlock(paragraphFormat(), bodyCharFormat(), false);
        break;
    }
    default:
        break;
    }
    return 0;
}

int Builder::leaveBlock(MD_BLOCKTYPE type, void *detail)
{
    Q_UNUSED(detail);
    switch (type) {
    case MD_BLOCK_DOC:
        break;
    case MD_BLOCK_QUOTE:
        popFrame();
        break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL:
        // A list that never got a materialized item stays pending.
        if (m_pendingListFormats.size() > m_lists.size())
            m_pendingListFormats.removeLast();
        else if (!m_lists.isEmpty())
            m_lists.removeLast();
        break;
    case MD_BLOCK_LI:
        startBlock(); // materialize empty items, e.g. "- " with no text
        m_inListItem = false;
        m_taskDone = false;
        m_taskPending = false;
        break;
    case MD_BLOCK_H:
        startBlock(); // materialize even when there was no text callback
        break;
    case MD_BLOCK_CODE:
        endVerbatimFrame();
        break;
    case MD_BLOCK_HTML:
        m_inHtmlBlock = false;
        renderHtmlBlock(m_htmlBuffer);
        m_htmlBuffer.clear();
        break;
    case MD_BLOCK_HR:
        startBlock();
        stampCurrentBlock(lineOfNextRule());
        m_cursor.insertText(QString(QChar(0x2500)).repeated(kRuleLength), ruleCharFormat());
        break;
    case MD_BLOCK_TABLE:
        emitTable();
        m_table.reset();
        break;
    case MD_BLOCK_TR:
        if (m_table && !m_table->currentRow.empty()) {
            if (m_table->currentRow.front().header)
                ++m_table->headerRows;
            m_table->rows.push_back(std::move(m_table->currentRow));
            m_table->currentRow.clear();
        }
        break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        if (m_cellStack.isEmpty())
            break;
        const SavedState saved = m_cellStack.pop();
        m_cursor = saved.cursor;
        m_pending = saved.pending;
        m_blockCharFormat = saved.blockCharFormat;
        m_span = saved.spans;
        m_linkHref = saved.linkHref;
        m_firstBlock = saved.firstBlock;
        m_style = saved.style;
        break;
    }
    default:
        break;
    }
    return 0;
}

int Builder::enterSpan(MD_SPANTYPE type, void *detail)
{
    switch (type) {
    case MD_SPAN_EM:
        m_span.em = true;
        break;
    case MD_SPAN_STRONG:
        m_span.strong = true;
        break;
    case MD_SPAN_DEL:
        m_span.del = true;
        break;
    case MD_SPAN_CODE:
        startBlock(); // inline code may open a tight list item
        m_span.code = true;
        break;
    case MD_SPAN_A: {
        const auto *link = static_cast<const MD_SPAN_A_DETAIL *>(detail);
        startBlock();
        m_linkHref = linkFor(decodeAttribute(link->href));
        break;
    }
    case MD_SPAN_IMG: {
        const auto *image = static_cast<const MD_SPAN_IMG_DETAIL *>(detail);
        startBlock();
        ++m_imageDepth;
        // The alt text arrives as text callbacks after this point; collect it
        // and insert the image (or a chip) on leave.
        m_pendingImageSrc = decodeAttribute(image->src);
        m_imageAltBuffer.clear();
        break;
    }
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        // The TeX source arrives as raw text callbacks after this point;
        // collect it and insert the object on leave, like an image's alt text.
        if (m_mathDepth == 0) {
            m_mathBuffer.clear();
            m_mathDisplay = type == MD_SPAN_LATEXMATH_DISPLAY;
        }
        ++m_mathDepth;
        break;
    default:
        break;
    }
    return 0;
}

int Builder::leaveSpan(MD_SPANTYPE type, void *detail)
{
    Q_UNUSED(detail);
    switch (type) {
    case MD_SPAN_EM:
        m_span.em = false;
        break;
    case MD_SPAN_STRONG:
        m_span.strong = false;
        break;
    case MD_SPAN_DEL:
        m_span.del = false;
        break;
    case MD_SPAN_CODE:
        m_span.code = false;
        break;
    case MD_SPAN_A:
        m_linkHref.clear();
        break;
    case MD_SPAN_IMG:
        --m_imageDepth;
        insertImageElement(m_pendingImageSrc, m_imageAltBuffer.trimmed());
        m_pendingImageSrc.clear();
        m_imageAltBuffer.clear();
        break;
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        --m_mathDepth;
        if (m_mathDepth == 0)
            insertMathElement(m_mathBuffer, m_mathDisplay);
        break;
    default:
        break;
    }
    return 0;
}

void Builder::startVerbatimFrame(const QColor &background, const QString &language)
{
    // Flush any pending block so the frame starts on a line of its own.
    startBlock();

    m_verbatimBlockFormat = codeFormat(language);
    m_verbatimPendingNewline = false;
    m_verbatimSourceLine = -1;
    m_inVerbatimBlock = true;

    // The frame box is the shaded region: it keeps long lines that wrap and
    // blank lines inside one clean rectangle, with padding all around.
    m_verbatimFrame = pushFrame(codeFrameFormat(background));
    m_verbatimCursor = m_cursor;
    m_verbatimCursor.setBlockFormat(m_verbatimBlockFormat);
    m_verbatimCursor.setBlockCharFormat(codeCharFormat());
    m_blockCharFormat = codeCharFormat();
}

void Builder::endVerbatimFrame()
{
    m_inVerbatimBlock = false;
    m_verbatimFrame = nullptr;
    popFrame();
}

void Builder::removeFrameSpacers()
{
    // Inserting frames leaves empty, unformatted blocks immediately before or
    // after them (and between two adjacent frames). They would render as extra
    // blank lines, so drop them.
    auto insideChildFrame = [this](const QTextBlock &block) {
        return block.isValid() && m_document->frameAt(block.position()) != m_document->rootFrame();
    };

    QVector<int> positions;
    for (QTextBlock block = m_document->firstBlock(); block.isValid(); block = block.next()) {
        if (!block.text().isEmpty())
            continue;
        if (block.blockFormat().intProperty(MarkdownBuilder::kindKey) != 0)
            continue;
        if (block.blockFormat().background().style() != Qt::NoBrush)
            continue;
        if (insideChildFrame(block))
            continue;
        if (insideChildFrame(block.previous()) || insideChildFrame(block.next()))
            positions.append(block.position());
    }

    for (int position : positions) {
        QTextCursor cursor(m_document);
        cursor.setPosition(position);
        // Deleting cannot cross a frame boundary, so collapse the spacer to
        // (almost) zero height instead of removing it.
        QTextBlockFormat format = cursor.blockFormat();
        format.setTopMargin(0);
        format.setBottomMargin(0);
        format.setLineHeight(0, QTextBlockFormat::FixedHeight);
        cursor.setBlockFormat(format);
    }
}

void Builder::insertVerbatim(const QString &content, int sourceLine)
{
    int line = sourceLine;

    if (m_verbatimSourceLine < 0) {
        // The first block of the frame was created before any text arrived.
        m_verbatimSourceLine = line;
        QTextBlockFormat format = m_verbatimBlockFormat;
        format.setProperty(MarkdownBuilder::sourceLineKey, line);
        m_verbatimCursor.setBlockFormat(format);
    }

    int from = 0;
    while (from < content.size()) {
        const int newline = int(content.indexOf(QLatin1Char('\n'), from));
        const QString text = newline < 0 ? content.mid(from) : content.mid(from, newline - from);

        if (m_verbatimPendingNewline) {
            QTextBlockFormat format = m_verbatimBlockFormat;
            format.setProperty(MarkdownBuilder::sourceLineKey, line);
            m_verbatimCursor.insertBlock(format, m_blockCharFormat);
            m_verbatimPendingNewline = false;
        }
        if (!text.isEmpty())
            m_verbatimCursor.insertText(text, composeCharFormat());
        if (newline < 0)
            break;
        m_verbatimPendingNewline = true;
        ++line;
        from = newline + 1;
    }
}

int Builder::text(MD_TEXTTYPE type, const char *data, unsigned size)
{
    // Keep the source cursor in step so every block knows the line it came
    // from. Soft and hard breaks decode to characters that are not in the
    // source at all; the next literal run resynchronises.
    const QString raw = QString::fromUtf8(data, int(size));
    int sourceLine = m_sourceLine;
    if (type != MD_TEXT_BR && type != MD_TEXT_SOFTBR) {
        sourceLine = consumeSource(raw);
        if (m_pending.valid)
            m_pending.sourceLine = sourceLine; // stamped when the block is opened
        else if (!m_inVerbatimBlock)
            stampCurrentBlock(sourceLine); // a span opened the block first
    }

    if (m_mathDepth > 0) {
        // Raw TeX: no entity decoding, no inline processing, verbatim.
        m_mathBuffer += QString::fromUtf8(data, int(size));
        return 0;
    }

    if (m_imageDepth > 0) {
        // Alt text of an image: kept to label a chip when the source is remote.
        switch (type) {
        case MD_TEXT_ENTITY:
            m_imageAltBuffer += decodeEntityText(data, size);
            break;
        case MD_TEXT_NULLCHAR:
            m_imageAltBuffer += QChar::ReplacementCharacter;
            break;
        case MD_TEXT_BR:
        case MD_TEXT_SOFTBR:
            m_imageAltBuffer += QLatin1Char(' ');
            break;
        default:
            m_imageAltBuffer += QString::fromUtf8(data, int(size));
            break;
        }
        return 0;
    }

    QString decoded;
    switch (type) {
    case MD_TEXT_BR:
        startBlock();
        m_cursor.insertText(QString(QChar(0x2028)), composeCharFormat());
        return 0;
    case MD_TEXT_SOFTBR:
        decoded = QStringLiteral(" ");
        break;
    case MD_TEXT_ENTITY:
        decoded = decodeEntityText(data, size);
        break;
    case MD_TEXT_NULLCHAR:
        decoded = QChar::ReplacementCharacter;
        break;
    default:
        decoded = QString::fromUtf8(data, int(size));
        break;
    }

    if (m_inHtmlBlock) {
        m_htmlBuffer += QString::fromUtf8(data, int(size));
        return 0;
    }

    if (type == MD_TEXT_HTML) {
        applyInlineHtml(QString::fromUtf8(data, int(size)));
        return 0;
    }

    if (m_inKbd && type != MD_TEXT_BR) {
        m_kbdBuffer += type == MD_TEXT_ENTITY ? decodeEntityText(data, size) : QString::fromUtf8(data, int(size));
        return 0;
    }

    if (m_inVerbatimBlock) {
        insertVerbatim(decoded, sourceLine);
        return 0;
    }

    startBlock();

    if (!decoded.isEmpty())
        m_cursor.insertText(decoded, composeCharFormat());
    return 0;
}

void Builder::insertImageElement(const QString &source, const QString &alt, HtmlLength width)
{
    const QString path = ImageResolver::resolve(source, m_style.baseDir);

    // An image wrapped in a link must carry the anchor itself: hover and click
    // hit tests read the char format under the cursor, and an object format
    // without one makes a GitHub-style badge inert.
    const auto linkAnchor = [this](QTextCharFormat &format) {
        if (!m_linkHref.isEmpty()) {
            format.setAnchor(true);
            format.setAnchorHref(m_linkHref);
        }
    };

    // An absolute width is capped by the image bound; a percentage asks for the
    // text column and is deliberately allowed past it, like a GitHub hero. The
    // column is the one the view actually has right now, falling back to the
    // cap when no live width was supplied.
    int desiredWidth = 0;
    if (width.percent > 0) {
        const int column = m_style.contentWidth > 0 ? m_style.contentWidth : m_style.maxContentWidth;
        desiredWidth = qRound(width.percent / 100.0 * column);
    } else if (width.pixels > 0) {
        desiredWidth = (std::min)(width.pixels, m_style.maxImageWidth);
    }

    // A span opens the block before its text arrives, so a block whose only
    // content is an image with no alt text still has no line. The image's own
    // path is the next thing in the source, on the same line.
    if (alt.isEmpty())
        stampCurrentBlock(consumeSource(source));

    // Remote references are downloaded by the viewer, not read from disk. The
    // builder only records the URL; the fetch and its guard rails live in
    // ImageFetcher. With remote images off -- or when the download fails -- the
    // alt-text chip carries the meaning instead.
    if (ImageResolver::isRemote(source)) {
        if (m_style.remoteImagesEnabled) {
            QTextImageFormat format;
            format.setName(source);
            format.setProperty(MarkdownBuilder::imageAltKey, alt);
            if (desiredWidth > 0)
                format.setWidth(desiredWidth);
            linkAnchor(format);
            m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), format);
            return;
        }
        if (!alt.isEmpty()) {
            insertChip(alt, MarkdownBuilder::ChipKind::Badge);
            return;
        }
    }

    // A remote image (e.g. a shields.io badge) is not fetched: show its alt
    // text as a chip so the document stays readable offline.
    if (path.isEmpty() && !alt.isEmpty()) {
        insertChip(alt, MarkdownBuilder::ChipKind::Badge);
        return;
    }

    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QTextCharFormat missing = m_blockCharFormat;
        missing.setForeground(m_style.theme.markdown.weakText);
        missing.setFontItalic(true);
        m_cursor.insertText(QStringLiteral("[image: %1]").arg(source), missing);
        return;
    }

    if (ImageResolver::isAnimated(path)) {
        QTextCharFormat format;
        format.setObjectType(MarkdownBuilder::gifObjectType);
        format.setProperty(MarkdownBuilder::gifPathKey, path);
        linkAnchor(format);
        m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), format);
        return;
    }

    const bool isSvg = path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive);

    QSize natural;
    if (isSvg) {
        QSvgRenderer renderer(path);
        natural = renderer.defaultSize();
    }
    if (!natural.isValid())
        natural = QImageReader(path).size();

    QSize display = natural;
    if (natural.isValid() && natural.width() > 0) {
        // An explicit width wins; otherwise the natural size is capped.
        const int desired = desiredWidth > 0 ? desiredWidth : (std::min)(natural.width(), m_style.maxImageWidth);
        if (desired > 0 && desired != natural.width()) {
            display.setWidth(desired);
            display.setHeight(qRound(qreal(natural.height()) * desired / natural.width()));
        }
    }

    // SVG is resolution-independent: rasterise it at the device pixel size with
    // antialiasing and hand it over as a resource, so it stays crisp.
    if (isSvg && display.isValid()) {
        const QSize pixelSize = (QSizeF(display) * devicePixelRatio()).toSize();
        const QImage rasterised = rasteriseSvg(path, pixelSize);
        if (!rasterised.isNull()) {
            const QString name = QStringLiteral("mmd-res-") + QString::number(qHash(path), 16);
            m_document->addResource(QTextDocument::ImageResource, QUrl(name), rasterised);

            QTextImageFormat format;
            format.setName(name);
            format.setWidth(display.width());
            format.setHeight(display.height());
            linkAnchor(format);
            m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), format);
            return;
        }
    }

    // A raster image that is scaled (a big screenshot, a logo): smooth it once
    // here and hand it over as a resource, so the document paints it 1:1.
    // Left to the text layout, a scaled image is resampled without
    // interpolation and looks rough.
    if (!isSvg && display.isValid() && display != natural) {
        QImage raster(path);
        if (!raster.isNull()) {
            const qreal ratio = devicePixelRatio();
            const QSize pixelSize = (QSizeF(display) * ratio).toSize();
            QImage scaled = raster.scaled(pixelSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (!scaled.isNull()) {
                scaled.setDevicePixelRatio(ratio);
                const QString name = QStringLiteral("mmd-res-") + QString::number(qHash(path), 16) + QLatin1Char('-') + QString::number(display.width());
                m_document->addResource(QTextDocument::ImageResource, QUrl(name), scaled);

                QTextImageFormat format;
                format.setName(name);
                format.setWidth(display.width());
                format.setHeight(display.height());
                linkAnchor(format);
                m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), format);
                return;
            }
        }
    }

    QTextImageFormat format;
    format.setName(path);
    if (display.isValid() && display != natural) {
        format.setWidth(display.width());
        format.setHeight(display.height());
    }
    linkAnchor(format);
    m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), format);
}

void Builder::emitTable()
{
    if (!m_table || m_table->rows.empty() || m_table->columns == 0)
        return;
    TableState table = std::move(*m_table);

    if (!m_firstBlock)
        m_cursor.insertBlock(QTextBlockFormat(), bodyCharFormat());
    m_firstBlock = false;

    QTextTableFormat format;
    format.setBorder(1);
    format.setBorderBrush(QBrush(m_style.theme.markdown.tableBorder));
    format.setCellSpacing(0);
    format.setCellPadding(8);
    format.setHeaderRowCount(int(table.headerRows));
    QVector<QTextLength> constraints;
    for (unsigned column = 0; column < table.columns; ++column)
        constraints.append(QTextLength(QTextLength::PercentageLength, 100.0 / table.columns));
    format.setColumnWidthConstraints(constraints);

    QTextTable *target = m_cursor.insertTable(int(table.rows.size()), int(table.columns), format);
    for (std::size_t row = 0; row < table.rows.size(); ++row) {
        const std::size_t columns = (std::min)(std::size_t(table.columns), table.rows[row].size());
        for (std::size_t column = 0; column < columns; ++column) {
            const Cell &cell = table.rows[row][column];
            QTextCursor cellCursor = target->cellAt(int(row), int(column)).firstCursorPosition();
            if (cell.document) {
                copyHtmlResources(*cell.document);
                cellCursor.insertFragment(QTextDocumentFragment(cell.document.get()));
            }
            if (cell.alignment != Qt::AlignLeft) {
                QTextBlockFormat blockFormat = cellCursor.blockFormat();
                blockFormat.setAlignment(cell.alignment);
                cellCursor.setBlockFormat(blockFormat);
            }
            target->cellAt(int(row), int(column)).setFormat(tableCellFormat(cell.header));
        }
    }

    m_cursor = QTextCursor(m_document);
    m_cursor.movePosition(QTextCursor::End);
}

void Builder::insertMathElement(const QString &tex, bool display)
{
    const QString source = tex.trimmed();
    if (source.isEmpty())
        return;

    QTextCharFormat format;
    format.setObjectType(MarkdownBuilder::mathObjectType);
    format.setProperty(MarkdownBuilder::mathSourceKey, source);
    format.setProperty(MarkdownBuilder::mathDisplayKey, display);
    const QString object = QString(QChar::ObjectReplacementCharacter);

    if (!display) {
        m_cursor.insertText(object, format);
        return;
    }
    // md4c glues a standalone $$...$$ to the paragraph before it: its lazy-math
    // rule swallows the blank line, so the span often arrives inside a block
    // that already carries text. Split it off -- the formula gets a centred
    // block of its own instead of centring somebody else's paragraph.
    QTextBlockFormat centre = paragraphFormat();
    centre.setAlignment(Qt::AlignHCenter);

    if (m_cursor.block().length() > 1) {
        m_pending = PendingBlock{}; // the block break is centred, nothing pending
        m_cursor.insertBlock(centre, m_blockCharFormat);
    } else {
        // The formula opens a fresh block: make that block the centred one.
        setPendingBlock(centre, m_blockCharFormat, false);
        startBlock();
    }

    // The block the formula just opened still needs its line; the TeX source it
    // was built from is what says which one.
    stampCurrentBlock(m_lastRunLine);
    m_cursor.insertText(object, format);
}

void Builder::insertChip(const QString &text, MarkdownBuilder::ChipKind kind)
{
    QTextCharFormat format = m_blockCharFormat;
    format.setObjectType(MarkdownBuilder::chipObjectType);
    format.setProperty(MarkdownBuilder::chipTextKey, text);
    format.setProperty(MarkdownBuilder::chipKindKey, int(kind));
    QFont font = m_style.fonts.bodyFont();
    font.setPointSizeF(font.pointSizeF() * 0.92);
    format.setFont(font);
    m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), format);
}

void Builder::applyInlineHtml(const QString &html, bool openBlock)
{
    const QVector<HtmlToken> tokens = parseHtmlFragment(html);
    // A block-level caller opens exactly one block for the whole run, so a row
    // of badges stays on a line. An inline caller (HTML inside a markdown
    // paragraph) only materializes a still-pending block; the tags belong to
    // the paragraph already in progress.
    if (m_pending.valid) {
        startBlock();
        m_htmlInlineContinued = false; // a markdown paragraph, not an HTML run
    } else if (openBlock) {
        startHtmlBlock();
        m_htmlInlineContinued = true;
    }
    // Otherwise this continues the HTML-inline block already open, so several
    // rows of badges that md4c hands over as separate blocks stay on one line.

    for (const HtmlToken &token : tokens) {
        switch (token.kind) {
        case HtmlToken::Text:
            if (!token.text.isEmpty()) {
                // A '\n' between inline tags is a soft break, not a paragraph:
                // inserting it verbatim would split a row of badges into one
                // block per badge. Keep it on the line as a space.
                QString text = token.text;
                text.replace(QLatin1Char('\n'), QLatin1Char(' '));
                m_cursor.insertText(text, composeCharFormat());
            }
            break;
        case HtmlToken::Comment:
        case HtmlToken::Declaration:
            break;
        case HtmlToken::StartTag:
        case HtmlToken::SelfClosingTag: {
            const QString &name = token.name;
            if (name == QLatin1String("b") || name == QLatin1String("strong")) {
                m_span.strong = true;
                m_htmlTagStack.append(name);
            } else if (name == QLatin1String("i") || name == QLatin1String("em")) {
                m_span.em = true;
                m_htmlTagStack.append(name);
            } else if (name == QLatin1String("code") || name == QLatin1String("tt")) {
                m_span.code = true;
                m_htmlTagStack.append(name);
            } else if (name == QLatin1String("sub")) {
                ++m_span.sub;
                m_htmlTagStack.append(name);
            } else if (name == QLatin1String("sup")) {
                ++m_span.sup;
                m_htmlTagStack.append(name);
            } else if (name == QLatin1String("kbd")) {
                m_inKbd = true;
                m_kbdBuffer.clear();
                m_htmlTagStack.append(name);
            } else if (name == QLatin1String("a")) {
                m_linkHref = linkFor(htmlAttribute(token, QStringLiteral("href")));
                m_htmlTagStack.append(name);
            } else if (name == QLatin1String("br")) {
                m_cursor.insertText(QString(QChar(0x2028)), composeCharFormat());
                // A hard break ends the HTML run: the next inline HTML block
                // starts its own block. Without this a standalone <br> merged
                // with the hero image that followed, adding an empty line and a
                // stray space that pushed the full-width image past the column.
                m_htmlInlineContinued = false;
            } else if (name == QLatin1String("img")) {
                insertImageElement(htmlAttribute(token, QStringLiteral("src")),
                                   htmlAttribute(token, QStringLiteral("alt")),
                                   parseHtmlLength(htmlAttribute(token, QStringLiteral("width"))));
            } else if (!token.selfClosing && !isHtmlVoidElement(name)) {
                m_htmlTagStack.append(QString()); // unknown container: balanced no-op
            }
            break;
        }
        case HtmlToken::EndTag: {
            if (m_htmlTagStack.isEmpty())
                break;
            const QString open = m_htmlTagStack.takeLast();
            if (open == QLatin1String("b") || open == QLatin1String("strong"))
                m_span.strong = false;
            else if (open == QLatin1String("i") || open == QLatin1String("em"))
                m_span.em = false;
            else if (open == QLatin1String("code") || open == QLatin1String("tt"))
                m_span.code = false;
            else if (open == QLatin1String("sub"))
                m_span.sub = (std::max)(0, m_span.sub - 1);
            else if (open == QLatin1String("sup"))
                m_span.sup = (std::max)(0, m_span.sup - 1);
            else if (open == QLatin1String("kbd")) {
                m_inKbd = false;
                insertChip(m_kbdBuffer.trimmed(), MarkdownBuilder::ChipKind::Key);
                m_kbdBuffer.clear();
            } else if (open == QLatin1String("a"))
                m_linkHref.clear();
            break;
        }
        }
    }
}

void Builder::insertSummary(const QString &html)
{
    // QTextDocument cannot collapse, so <summary> becomes a marked bold line.
    setPendingBlock(paragraphFormat(), bodyCharFormat(), false);
    startBlock();

    QTextCharFormat marker = bodyCharFormat();
    marker.setForeground(m_style.theme.markdown.weakText);
    m_cursor.insertText(QStringLiteral("\u25B8 "), marker);

    QTextCharFormat title = bodyCharFormat();
    title.setFontWeight(QFont::Bold);
    // The inner content is usually inline HTML; keep its text.
    QString inner;
    for (const HtmlToken &token : parseHtmlFragment(html)) {
        if (token.kind == HtmlToken::Text)
            inner += token.text;
    }
    m_cursor.insertText(inner.trimmed(), title);
}

// Starts a block for raw HTML content (an image, a fallback line) whose own
// tag opens it rather than a markdown span. Alignment inherited from an open
// block-level container is applied, so an image inside <div align="center">
// lands centred.
void Builder::startHtmlBlock()
{
    // An HTML block is usually an image or a row of badges. The paragraph's
    // 122% line height is meant for prose; applied to an image line it stretches
    // the line by a fifth of the image, leaving a visible gap below it (a
    // 231px banner advanced 281px). A plain block keeps the image at its size.
    QTextBlockFormat format;
    format.setBottomMargin(3);
    if (!m_openHtmlBlocks.isEmpty()) {
        const Qt::Alignment alignment = m_openHtmlBlocks.last().second;
        if (alignment != Qt::Alignment())
            format.setAlignment(alignment);
    }

    if (m_firstBlock) {
        m_cursor.setBlockFormat(format);
        m_cursor.setBlockCharFormat(bodyCharFormat());
    } else {
        m_cursor.insertBlock(format, bodyCharFormat());
    }
    m_firstBlock = false;
}

// A block for an inline tag: a pending markdown block is materialized as usual,
// otherwise this is HTML content opening its own block.
// CommonMark ends an HTML block at a blank line, so a <table> written with
// blank lines between its parts (as NEXUS.md does) reaches the builder as a
// scatter of HTML fragments and markdown paragraphs. Pull each whole table out
// of the source before parsing and leave a one-line marker behind: the table
// is then rendered in one piece, and the line count is preserved so
// click-to-source stays correct for everything around it.
static QString extractHtmlTables(const QString &source, QVector<QString> &tables)
{
    QStringList lines = source.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (!lines[i].trimmed().startsWith(QLatin1String("<table"), Qt::CaseInsensitive))
            continue;

        int j = i;
        while (j < lines.size() && !lines[j].contains(QLatin1String("</table>"), Qt::CaseInsensitive))
            ++j;
        if (j >= lines.size())
            break; // unterminated: leave it alone

        const int index = int(tables.size());
        tables.append(lines.mid(i, j - i + 1).join(QLatin1Char('\n')));
        lines[i] = QStringLiteral("<!--mmd-html-table-%1-->").arg(index);
        for (int k = i + 1; k <= j; ++k)
            lines[k].clear();
        i = j;
    }
    return lines.join(QLatin1Char('\n'));
}

void Builder::renderHtmlBlock(const QString &html)
{
    // A marker left by the table pre-pass: render the table we extracted.
    const QString trimmed = html.trimmed();
    static const QRegularExpression tableMarker(QStringLiteral("^<!--mmd-html-table-(\\d+)-->$"));
    const QRegularExpressionMatch match = tableMarker.match(trimmed);
    if (match.hasMatch()) {
        const int index = match.captured(1).toInt();
        if (index >= 0 && index < m_extractedTables.size()) {
            const QString &raw = m_extractedTables.at(index);
            const int open = int(raw.indexOf(QLatin1Char('>')));
            const int close = int(raw.lastIndexOf(QLatin1String("</table>"), -1, Qt::CaseInsensitive));
            if (open >= 0 && close > open)
                renderHtmlTable(raw.mid(open + 1, close - open - 1));
        }
        return;
    }
    const QVector<HtmlToken> tokens = parseHtmlFragment(html);
    renderHtmlFragment(html, tokens, 0, int(tokens.size()));
}

// Tags whose content is a run of blocks; the renderer recurses into them
// instead of treating them as inline text.
static bool isHtmlBlockContainer(const QString &name)
{
    static const QSet<QString> containers = {
        QStringLiteral("p"),       QStringLiteral("div"),        QStringLiteral("center"),     QStringLiteral("section"), QStringLiteral("article"),
        QStringLiteral("header"),  QStringLiteral("footer"),     QStringLiteral("main"),       QStringLiteral("aside"),   QStringLiteral("nav"),
        QStringLiteral("figure"),  QStringLiteral("figcaption"), QStringLiteral("blockquote"), QStringLiteral("pre"),     QStringLiteral("table"),
        QStringLiteral("details"), QStringLiteral("summary"),    QStringLiteral("ul"),         QStringLiteral("ol"),      QStringLiteral("li"),
        QStringLiteral("dl"),      QStringLiteral("dt"),         QStringLiteral("dd"),         QStringLiteral("h1"),      QStringLiteral("h2"),
        QStringLiteral("h3"),      QStringLiteral("h4"),         QStringLiteral("h5"),         QStringLiteral("h6"),
    };
    return containers.contains(name);
}

static int findHtmlMatchingEnd(const QVector<HtmlToken> &tokens, int openIndex, int end, const QString &name)
{
    int depth = 1;
    for (int i = openIndex + 1; i < end; ++i) {
        const HtmlToken &token = tokens[i];
        if (token.kind == HtmlToken::StartTag && token.name == name && !token.selfClosing)
            ++depth;
        else if (token.kind == HtmlToken::EndTag && token.name == name && --depth == 0)
            return i;
    }
    return -1;
}

static Qt::Alignment htmlAlignment(const HtmlToken &token)
{
    const QString align = htmlAttribute(token, QStringLiteral("align"));
    if (align.compare(QLatin1String("center"), Qt::CaseInsensitive) == 0)
        return Qt::AlignHCenter;
    if (align.compare(QLatin1String("right"), Qt::CaseInsensitive) == 0)
        return Qt::AlignRight;
    return Qt::Alignment();
}

// Walks one range of tokens. Block containers are rendered structurally;
// everything else (text and inline tags) is handed to the inline renderer in
// runs, so a paragraph of mixed markup keeps its emphasis, links and images.
void Builder::renderHtmlFragment(const QString &html, const QVector<HtmlToken> &tokens, int begin, int end)
{
    int runStart = -1;

    const auto flushInline = [&](int upto) {
        if (runStart < 0)
            return;
        const int from = tokens[runStart].start;
        const int to = tokens[upto].end;
        runStart = -1;
        if (to > from) {
            const QString run = html.mid(from, to - from);
            // Whitespace between block tags carries no content and must not
            // become a stray paragraph.
            if (!run.trimmed().isEmpty())
                applyInlineHtml(run, !m_htmlInlineContinued);
        }
    };

    for (int i = begin; i < end; ++i) {
        const HtmlToken &token = tokens[i];

        if ((token.kind == HtmlToken::StartTag || token.kind == HtmlToken::SelfClosingTag) && isHtmlBlockContainer(token.name)) {
            flushInline(i);
            const int close = findHtmlMatchingEnd(tokens, i, end, token.name);
            if (close >= 0) {
                const int innerFrom = token.end;
                const int innerTo = tokens[close].start;
                renderHtmlContainer(token, html.mid(innerFrom, (std::max)(innerFrom, innerTo) - innerFrom));
                i = close;
            } else {
                // No closing tag here: the container runs past this block, so
                // remember it and let startBlock() align what follows.
                m_openHtmlBlocks.append({token.name, htmlAlignment(token)});
                m_htmlInlineContinued = false;
            }
            continue;
        }

        if (token.kind == HtmlToken::EndTag && isHtmlBlockContainer(token.name)) {
            // Closes a container opened in an earlier block.
            if (!m_openHtmlBlocks.isEmpty() && m_openHtmlBlocks.last().first == token.name)
                m_openHtmlBlocks.removeLast();
            continue;
        }

        if (token.kind == HtmlToken::Text || token.kind == HtmlToken::StartTag || token.kind == HtmlToken::EndTag || token.kind == HtmlToken::SelfClosingTag
            || token.kind == HtmlToken::Comment) {
            if (runStart < 0)
                runStart = i;
        }
    }
    flushInline(end - 1);
}

void Builder::renderHtmlContainer(const HtmlToken &token, const QString &innerHtml)
{
    const QString name = token.name;

    if (name == QLatin1String("table")) {
        renderHtmlTable(innerHtml);
        return;
    }
    if (name == QLatin1String("summary")) {
        insertSummary(innerHtml);
        return;
    }
    if (name == QLatin1String("details")) {
        ++m_htmlDetailsDepth;
        const QVector<HtmlToken> tokens = parseHtmlFragment(innerHtml);
        renderHtmlFragment(innerHtml, tokens, 0, int(tokens.size()));
        --m_htmlDetailsDepth;
        return;
    }
    if (name == QLatin1String("blockquote")) {
        pushQuote();
        renderHtmlSubdocument(innerHtml, htmlAlignment(token));
        popFrame();
        return;
    }

    renderHtmlSubdocument(innerHtml, htmlAlignment(token));
}

// Renders the markdown-and-HTML inside a container by running it back through
// the builder in a scratch document, then pasting the result in. This is what
// makes <td> cells that contain markdown work the way GitHub spells them.
void Builder::renderHtmlSubdocument(const QString &innerHtml, Qt::Alignment alignment)
{
    if (innerHtml.trimmed().isEmpty())
        return;

    QTextDocument sub;
    MarkdownBuilder::build(&sub, innerHtml, m_style);
    if (sub.isEmpty())
        return;
    copyHtmlResources(sub);
    insertHtmlSubdocument(sub, alignment);
}

void Builder::insertHtmlSubdocument(const QTextDocument &source, Qt::Alignment alignment)
{
    if (!m_firstBlock)
        m_cursor.insertBlock(paragraphFormat(), bodyCharFormat());
    m_firstBlock = false;

    const int firstBlockNumber = m_cursor.block().blockNumber();
    // insertFragment folds the fragment's first block into the current one and
    // appends the rest, so the range it occupies is the fragment's block count.
    const int lastBlockNumber = firstBlockNumber + source.blockCount() - 1;
    m_cursor.insertFragment(QTextDocumentFragment(&source));

    if (alignment != Qt::Alignment()) {
        // The fragment copy replaced the current block and appended the rest;
        // walk the block numbers it occupies and align each.
        for (int number = firstBlockNumber; number <= lastBlockNumber; ++number) {
            const QTextBlock block = m_document->findBlockByNumber(number);
            if (!block.isValid())
                continue;
            QTextBlockFormat format = block.blockFormat();
            format.setAlignment(alignment);
            QTextCursor cursor(block);
            cursor.setBlockFormat(format);
        }
    }

    // Leave the cursor at the very end so the next block lands after this one.
    m_cursor.setPosition(m_document->characterCount() - 1);
    m_htmlInlineContinued = false;
}

void Builder::copyHtmlResources(const QTextDocument &source)
{
    // A fragment copy carries formats, not document resources: any SVG the
    // inner build rasterised has to be re-registered on our own document or
    // the image would come out blank in the outer one.
    for (QTextBlock block = source.firstBlock(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (!format.isImageFormat())
                continue;
            const QUrl url(format.toImageFormat().name());
            if (!url.isValid() || m_document->resource(QTextDocument::ImageResource, url).isValid())
                continue;
            const QVariant resource = source.resource(QTextDocument::ImageResource, url);
            if (!resource.isNull())
                m_document->addResource(QTextDocument::ImageResource, url, resource);
        }
    }
}

void Builder::renderHtmlTable(const QString &innerHtml)
{
    const QVector<HtmlToken> tokens = parseHtmlFragment(innerHtml);

    struct HtmlCell {
        QString content;
        bool header = false;
        int colspan = 1;
        int rowspan = 1;
        HtmlLength width;
        Qt::Alignment alignment;
        QTextCharFormat::VerticalAlignment verticalAlignment = QTextCharFormat::AlignMiddle;
    };
    QVector<QVector<HtmlCell>> rows;
    QTextCharFormat::VerticalAlignment currentValign = QTextCharFormat::AlignMiddle;

    for (int i = 0; i < tokens.size(); ++i) {
        const HtmlToken &token = tokens[i];
        if (token.kind != HtmlToken::StartTag)
            continue;
        if (token.name == QLatin1String("tr")) {
            rows.append(QVector<HtmlCell>());
            const QString rValign = htmlAttribute(token, QStringLiteral("valign")).toLower();
            if (rValign == QLatin1String("top"))
                currentValign = QTextCharFormat::AlignTop;
            else if (rValign == QLatin1String("bottom"))
                currentValign = QTextCharFormat::AlignBottom;
            else
                currentValign = QTextCharFormat::AlignMiddle;
            continue;
        }
        if (token.name != QLatin1String("td") && token.name != QLatin1String("th"))
            continue;
        if (rows.isEmpty())
            rows.append(QVector<HtmlCell>());

        const int close = findHtmlMatchingEnd(tokens, i, int(tokens.size()), token.name);
        const int innerTo = close >= 0 ? tokens[close].start : tokens.last().end;
        HtmlCell cell;
        cell.content = innerHtml.mid(token.end, (std::max)(token.end, innerTo) - token.end);
        cell.header = token.name == QLatin1String("th");
        const int colspan = htmlAttribute(token, QStringLiteral("colspan")).toInt();
        const int rowspan = htmlAttribute(token, QStringLiteral("rowspan")).toInt();
        if (colspan > 1)
            cell.colspan = colspan;
        if (rowspan > 1)
            cell.rowspan = rowspan;

        cell.width = parseHtmlLength(htmlAttribute(token, QStringLiteral("width")));
        if (cell.width.percent == 0 && cell.width.pixels == 0) {
            const QString styleAttr = htmlAttribute(token, QStringLiteral("style"));
            if (!styleAttr.isEmpty()) {
                static const QRegularExpression styleWidthRe(QStringLiteral(R"(width\s*:\s*([^;]+))"), QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch styleMatch = styleWidthRe.match(styleAttr);
                if (styleMatch.hasMatch()) {
                    QString wVal = styleMatch.captured(1).trimmed();
                    if (wVal.endsWith(QLatin1String("px"), Qt::CaseInsensitive))
                        wVal.chop(2);
                    cell.width = parseHtmlLength(wVal);
                }
            }
        }

        cell.alignment = htmlAlignment(token);
        cell.verticalAlignment = currentValign;
        const QString cValign = htmlAttribute(token, QStringLiteral("valign")).toLower();
        if (cValign == QLatin1String("top"))
            cell.verticalAlignment = QTextCharFormat::AlignTop;
        else if (cValign == QLatin1String("bottom"))
            cell.verticalAlignment = QTextCharFormat::AlignBottom;
        else if (cValign == QLatin1String("middle"))
            cell.verticalAlignment = QTextCharFormat::AlignMiddle;

        rows.last().append(cell);
        if (close >= 0)
            i = close;
    }

    rows.removeIf([](const QVector<HtmlCell> &row) {
        return row.isEmpty();
    });
    if (rows.isEmpty())
        return;

    unsigned columns = 0;
    for (const QVector<HtmlCell> &row : rows) {
        unsigned rowCols = 0;
        for (const HtmlCell &cell : row)
            rowCols += unsigned((std::max)(1, cell.colspan));
        columns = (std::max)(columns, rowCols);
    }
    if (columns == 0)
        return;

    if (!m_firstBlock)
        m_cursor.insertBlock(QTextBlockFormat(), bodyCharFormat());
    m_firstBlock = false;

    QTextTableFormat format;
    format.setBorder(1);
    format.setBorderBrush(QBrush(m_style.theme.markdown.tableBorder));
    format.setCellSpacing(0);
    format.setCellPadding(8);

    QVector<QTextLength> constraints;
    constraints.resize(qsizetype(columns));
    QVector<HtmlLength> specifiedWidths;
    specifiedWidths.resize(qsizetype(columns));

    for (const QVector<HtmlCell> &row : rows) {
        int colIdx = 0;
        for (const HtmlCell &cell : row) {
            if (cell.colspan <= 1 && colIdx < int(columns)) {
                if (specifiedWidths[colIdx].percent == 0 && specifiedWidths[colIdx].pixels == 0) {
                    if (cell.width.percent > 0 || cell.width.pixels > 0)
                        specifiedWidths[colIdx] = cell.width;
                }
            }
            colIdx += (std::max)(1, cell.colspan);
        }
    }

    qreal totalSpecifiedPercent = 0.0;
    int unspecifiedCount = 0;
    for (int c = 0; c < int(columns); ++c) {
        if (specifiedWidths[c].percent > 0)
            totalSpecifiedPercent += specifiedWidths[c].percent;
        else
            ++unspecifiedCount;
    }

    for (int c = 0; c < int(columns); ++c) {
        if (specifiedWidths[c].percent > 0) {
            constraints[c] = QTextLength(QTextLength::PercentageLength, specifiedWidths[c].percent);
        } else if (specifiedWidths[c].pixels > 0) {
            constraints[c] = QTextLength(QTextLength::FixedLength, specifiedWidths[c].pixels);
        } else {
            const qreal remaining = (std::max)(0.0, 100.0 - totalSpecifiedPercent);
            const qreal share = unspecifiedCount > 0 ? remaining / qreal(unspecifiedCount) : 100.0 / qreal(columns);
            constraints[c] = QTextLength(QTextLength::PercentageLength, share);
        }
    }
    format.setColumnWidthConstraints(constraints);

    const int totalTableWidth = m_style.contentWidth > 0 ? m_style.contentWidth : m_style.maxContentWidth;
    const int padding = qRound(format.cellPadding());

    const auto columnSpanWidth = [&](int startCol, int span) -> int {
        int widthPx = 0;
        const int endCol = (std::min)(int(columns), startCol + (std::max)(1, span));
        for (int c = startCol; c < endCol; ++c) {
            if (constraints[c].type() == QTextLength::PercentageLength) {
                widthPx += qRound(constraints[c].rawValue() / 100.0 * qreal(totalTableWidth));
            } else if (constraints[c].type() == QTextLength::FixedLength) {
                widthPx += qRound(constraints[c].rawValue());
            } else {
                widthPx += qRound(qreal(totalTableWidth) / qreal(columns));
            }
        }
        return (std::max)(40, widthPx - (2 * padding + 2));
    };

    QTextTable *table = m_cursor.insertTable(int(rows.size()), int(columns), format);
    for (int row = 0; row < rows.size(); ++row) {
        int currentCol = 0;
        for (int cellIdx = 0; cellIdx < rows[row].size(); ++cellIdx) {
            const HtmlCell &cell = rows[row][cellIdx];
            if (currentCol >= int(columns))
                break;

            const int spanCols = (std::max)(1, cell.colspan);
            const int spanRows = (std::max)(1, cell.rowspan);
            if (spanCols > 1 || spanRows > 1) {
                table->mergeCells(row, currentCol, (std::min)(int(rows.size()) - row, spanRows), (std::min)(int(columns) - currentCol, spanCols));
            }

            QTextCursor cellCursor = table->cellAt(row, currentCol).firstCursorPosition();

            const int cellAvailableWidth = columnSpanWidth(currentCol, spanCols);
            RenderStyle cellStyle = m_style;
            cellStyle.contentWidth = cellAvailableWidth;
            cellStyle.maxContentWidth = cellAvailableWidth;
            cellStyle.maxImageWidth = cellAvailableWidth;

            QTextDocument sub;
            MarkdownBuilder::build(&sub, cell.content, cellStyle);
            if (!sub.isEmpty()) {
                copyHtmlResources(sub);
                cellCursor.insertFragment(QTextDocumentFragment(&sub));
                if (cell.alignment != Qt::Alignment()) {
                    for (QTextBlock block = cellCursor.block(); block.isValid(); block = block.next()) {
                        QTextBlockFormat blockFormat = block.blockFormat();
                        blockFormat.setAlignment(cell.alignment);
                        QTextCursor alignCursor(block);
                        alignCursor.setBlockFormat(blockFormat);
                    }
                }
            }

            QTextTableCellFormat cellFormat = tableCellFormat(cell.header);
            cellFormat.setVerticalAlignment(cell.verticalAlignment);
            table->cellAt(row, currentCol).setFormat(cellFormat);

            currentCol += spanCols;
        }
    }

    m_cursor = QTextCursor(m_document);
    m_cursor.movePosition(QTextCursor::End);
}

void Builder::renderCallout(QTextFrame *quote)
{
    auto *table = qobject_cast<QTextTable *>(quote);
    if (!table)
        return;

    QTextTableCell cell = table->cellAt(0, 0);
    const QTextBlock first = cell.firstCursorPosition().block();
    static const QRegularExpression pattern(QStringLiteral("^\\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\\]\\s*"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(first.text());
    if (!match.hasMatch())
        return;

    const QString type = match.captured(1).toUpper();
    QColor color;
    QString label;
    if (type == QLatin1String("TIP")) {
        color = m_style.theme.markdown.taskDone;
        label = QStringLiteral("Tip");
    } else if (type == QLatin1String("IMPORTANT")) {
        color = m_style.theme.syntax.annotation;
        label = QStringLiteral("Important");
    } else if (type == QLatin1String("WARNING")) {
        color = m_style.theme.syntax.warning;
        label = QStringLiteral("Warning");
    } else if (type == QLatin1String("CAUTION")) {
        color = m_style.theme.syntax.error;
        label = QStringLiteral("Caution");
    } else {
        color = m_style.theme.markdown.link;
        label = QStringLiteral("Note");
    }

    // Replace the [!TYPE] marker with a coloured label; keep any text that
    // followed it on the same paragraph (a soft break) as a new block.
    const QString rest = first.text().mid(match.capturedLength(0)).trimmed();
    QTextCursor cursor(m_document);
    cursor.setPosition(first.position());
    cursor.setPosition(first.position() + first.length() - 1, QTextCursor::KeepAnchor);
    QTextCharFormat labelFormat;
    labelFormat.setFontWeight(QFont::Bold);
    labelFormat.setForeground(color);
    cursor.insertText(label, labelFormat);
    if (!rest.isEmpty()) {
        cursor.insertBlock(paragraphFormat(), bodyCharFormat());
        cursor.insertText(rest, bodyCharFormat());
    }

    // Tint the bar and give the box a faint wash of the type colour.
    QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();
    cellFormat.setLeftBorderBrush(color);
    QColor wash = color;
    wash.setAlpha(20);
    cellFormat.setBackground(wash);
    cell.setFormat(cellFormat);
}

} // namespace

bool MarkdownBuilder::build(QTextDocument *target, const QString &markdown, const RenderStyle &style)
{
    Builder builder(target, style);
    return builder.run(markdown);
}

QString MarkdownBuilder::headingSlug(const QString &heading)
{
    QString slug;
    slug.reserve(heading.size());
    for (const QChar character : heading) {
        if (character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('-'))
            slug += character.toLower();
        else if (character.isSpace())
            slug += QLatin1Char('-');
        // everything else (punctuation) is dropped, like GitHub does
    }
    return slug;
}
