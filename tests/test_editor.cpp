// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "editor/MarkdownEditor.h"
#include "editor/MarkdownHighlighter.h"

#include <QTextBlock>
#include <QTextDocument>

class EditorTest : public QObject
{
    Q_OBJECT

private slots:
    void headingLineHighlighted();
    void fenceBlocksGetCodeStyleAndState();
    void inlineCodeSpanHighlighted();
    void linkColored();
    void editorAppliesThemeAndCountsLines();
    void themeSwitchRehighlights();
    void highlightingCanBeDetached();
    void gotoSourceLineMovesTheCaretWithoutFocus();
    void currentLineHighlightUsesDedicatedColor();
    void wordWrapTogglesLineWrapMode();
    void toggleTaskCheckbox();
    void smartIndentAndListContinuation();
    void formattingShortcuts();

private:
    static QTextCharFormat formatAt(const QTextBlock &block, int position);
    static QTextDocument *makeDocument(const QString &markdown, MarkdownHighlighter **highlighter);
};

QTextCharFormat EditorTest::formatAt(const QTextBlock &block, int position)
{
    // QSyntaxHighlighter formats live in the block layout, not in fragments.
    const auto ranges = block.layout()->formats();
    for (const QTextLayout::FormatRange &range : ranges) {
        if (position >= range.start && position < range.start + range.length)
            return range.format;
    }
    for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
        if (it.fragment().contains(block.position() + position))
            return it.fragment().charFormat();
    }
    return QTextCharFormat();
}

QTextDocument *EditorTest::makeDocument(const QString &markdown, MarkdownHighlighter **highlighter)
{
    // The text must be set before the theme: see MarkdownEditor::setMarkdown
    // for why programmatic replacement needs an explicit rehighlight.
    auto *document = new QTextDocument;
    auto *hl = new MarkdownHighlighter(document);
    document->setPlainText(markdown);
    hl->setTheme(ThemeDefinition::light());
    *highlighter = hl;
    return document;
}

void EditorTest::headingLineHighlighted()
{
    const ThemeDefinition theme = ThemeDefinition::light();

    MarkdownHighlighter *highlighter = nullptr;
    std::unique_ptr<QTextDocument> document(makeDocument(QStringLiteral("# Title\n"), &highlighter));
    Q_UNUSED(highlighter);

    const QTextBlock block = document->firstBlock();
    QCOMPARE(formatAt(block, 0).fontWeight(), int(QFont::Bold));
    QCOMPARE(formatAt(block, 0).foreground().color(), theme.markdown.heading);
}

void EditorTest::fenceBlocksGetCodeStyleAndState()
{
    const ThemeDefinition theme = ThemeDefinition::light();

    MarkdownHighlighter *highlighter = nullptr;
    std::unique_ptr<QTextDocument> document(makeDocument(QStringLiteral("before\n```cpp\nint a;\n```\nafter\n"), &highlighter));
    Q_UNUSED(highlighter);

    QVector<QTextBlock> blocks;
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next())
        blocks.append(block);

    QCOMPARE(blocks.size(), 6); // five source lines plus the trailing empty block
    // Fence line and content are styled as code.
    QCOMPARE(formatAt(blocks[1], 0).background().color(), theme.markdown.codeBackground);
    QCOMPARE(formatAt(blocks[2], 0).background().color(), theme.markdown.codeBackground);
    // State machine closed the fence: "after" is plain again.
    QVERIFY(!formatAt(blocks[4], 0).hasProperty(QTextFormat::BackgroundBrush));
}

void EditorTest::inlineCodeSpanHighlighted()
{
    const ThemeDefinition theme = ThemeDefinition::light();

    MarkdownHighlighter *highlighter = nullptr;
    std::unique_ptr<QTextDocument> document(makeDocument(QStringLiteral("a `code` b\n"), &highlighter));
    Q_UNUSED(highlighter);

    const QTextBlock block = document->firstBlock();
    const QTextCharFormat format = formatAt(block, int(block.text().indexOf(QStringLiteral("code"))));
    QCOMPARE(format.background().color(), theme.markdown.codeBackground);
    QCOMPARE(format.foreground().color(), theme.markdown.codeText);
}

void EditorTest::linkColored()
{
    const ThemeDefinition theme = ThemeDefinition::light();

    MarkdownHighlighter *highlighter = nullptr;
    std::unique_ptr<QTextDocument> document(makeDocument(QStringLiteral("see [docs](https://example.com) now\n"), &highlighter));
    Q_UNUSED(highlighter);

    const QTextBlock block = document->firstBlock();
    const QTextCharFormat format = formatAt(block, int(block.text().indexOf(QStringLiteral("docs"))));
    QCOMPARE(format.foreground().color(), theme.markdown.link);
}

void EditorTest::editorAppliesThemeAndCountsLines()
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("one\ntwo\n"));
    QCOMPARE(editor.document()->blockCount(), 3); // trailing block after the last newline
    QVERIFY(editor.lineNumberAreaWidth() > 0);

    editor.setTheme(ThemeDefinition::dark());
    QCOMPARE(editor.document()->blockCount(), 3);
}

void EditorTest::themeSwitchRehighlights()
{
    MarkdownHighlighter *highlighter = nullptr;
    std::unique_ptr<QTextDocument> document(makeDocument(QStringLiteral("# Title\n"), &highlighter));

    const QColor before = formatAt(document->firstBlock(), 0).foreground().color();
    highlighter->setTheme(ThemeDefinition::dark());
    const QColor after = formatAt(document->firstBlock(), 0).foreground().color();
    QVERIFY(before != after);
}

void EditorTest::highlightingCanBeDetached()
{
    MarkdownEditor editor;
    editor.setDocumentLimits(DocumentLimits::forSize(8 * 1024 * 1024)); // Manual tier
    QVERIFY(!editor.highlightingEnabled());

    editor.setMarkdown(QStringLiteral("# Title\n"));
    QCOMPARE(editor.document()->blockCount(), 2);
    // No highlighter attached, so the heading stays plain.
    QVERIFY(formatAt(editor.document()->firstBlock(), 0).fontWeight() != int(QFont::Bold));

    // An ordinary-sized document brings the colours back on screen.
    editor.setDocumentLimits(DocumentLimits{});
    QVERIFY(editor.highlightingEnabled());
    QCOMPARE(formatAt(editor.document()->firstBlock(), 0).fontWeight(), int(QFont::Bold));
}

void EditorTest::gotoSourceLineMovesTheCaretWithoutFocus()
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("# Title\n\nfirst\n\nsecond\n"));
    QVERIFY(!editor.hasFocus());

    editor.gotoSourceLine(4);

    QCOMPARE(editor.textCursor().block().blockNumber(), 4);
    QCOMPARE(editor.textCursor().block().text(), QStringLiteral("second"));
    QVERIFY(!editor.textCursor().hasSelection());
    // Moving the caret must not steal the focus: the click came from the
    // preview, which keeps it, so text there can still be selected and copied.
    QVERIFY(!editor.hasFocus());
}

void EditorTest::currentLineHighlightUsesDedicatedColor()
{
    const ThemeDefinition theme = ThemeDefinition::light();

    MarkdownEditor editor;
    editor.setTheme(theme);
    editor.setMarkdown(QStringLiteral("```cpp\nint a;\nint b;\n```\n"));
    // Park the caret on a line inside the fenced block: there the highlight used
    // to vanish, painted with the very color of the block's background.
    editor.moveCursor(QTextCursor::Start);
    editor.moveCursor(QTextCursor::Down, QTextCursor::MoveAnchor);

    const QList<QTextEdit::ExtraSelection> selections = editor.extraSelections();
    QVERIFY(!selections.isEmpty());
    QCOMPARE(selections.first().format.background().color(), theme.markdown.currentLine);
}

void EditorTest::wordWrapTogglesLineWrapMode()
{
    MarkdownEditor editor;
    QVERIFY(!editor.wordWrap());
    QCOMPARE(editor.lineWrapMode(), QPlainTextEdit::NoWrap);

    editor.setWordWrap(true);
    QVERIFY(editor.wordWrap());
    QCOMPARE(editor.lineWrapMode(), QPlainTextEdit::WidgetWidth);

    editor.setWordWrap(false);
    QVERIFY(!editor.wordWrap());
    QCOMPARE(editor.lineWrapMode(), QPlainTextEdit::NoWrap);
}

void EditorTest::toggleTaskCheckbox()
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("- [ ] first item\n* [x] second item\nregular line\n"));

    // Toggle unchecked to checked on line 0
    QVERIFY(editor.toggleTaskCheckboxAtSourceLine(0));
    QCOMPARE(editor.document()->findBlockByLineNumber(0).text(), QStringLiteral("- [x] first item"));

    // Toggle checked to unchecked on line 0
    QVERIFY(editor.toggleTaskCheckboxAtSourceLine(0));
    QCOMPARE(editor.document()->findBlockByLineNumber(0).text(), QStringLiteral("- [ ] first item"));

    // Toggle checked to unchecked on line 1
    QVERIFY(editor.toggleTaskCheckboxAtSourceLine(1));
    QCOMPARE(editor.document()->findBlockByLineNumber(1).text(), QStringLiteral("* [ ] second item"));

    // Regular line has no checkbox
    QVERIFY(!editor.toggleTaskCheckboxAtSourceLine(2));
    // Out of range
    QVERIFY(!editor.toggleTaskCheckboxAtSourceLine(99));
    QVERIFY(!editor.toggleTaskCheckboxAtSourceLine(-1));
}

void EditorTest::smartIndentAndListContinuation()
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("- first item"));
    editor.moveCursor(QTextCursor::End);

    // Press Return on non-empty list item
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("- first item\n- "));

    // Press Return on empty list item cancels list marker
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("- first item\n"));

    // Task list continuation
    editor.setMarkdown(QStringLiteral("- [ ] task 1"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("- [ ] task 1\n- [ ] "));

    // Ordered list continuation
    editor.setMarkdown(QStringLiteral("1. first"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("1. first\n2. "));
}

void EditorTest::formattingShortcuts()
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("hello world"));

    // Select "world"
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(6);
    cursor.setPosition(11, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);

    // Ctrl+I toggles italic
    QTest::keyClick(&editor, Qt::Key_I, Qt::ControlModifier);
    QCOMPARE(editor.toPlainText(), QStringLiteral("hello *world*"));

    // Heading shortcut Ctrl+2
    editor.moveCursor(QTextCursor::Start);
    QTest::keyClick(&editor, Qt::Key_2, Qt::ControlModifier);
    QCOMPARE(editor.toPlainText(), QStringLiteral("## hello *world*"));

    // Pressing Ctrl+2 again removes heading
    QTest::keyClick(&editor, Qt::Key_2, Qt::ControlModifier);
    QCOMPARE(editor.toPlainText(), QStringLiteral("hello *world*"));
}

QTEST_MAIN(EditorTest)
#include "test_editor.moc"