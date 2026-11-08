// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "editor/MarkdownEditor.h"

#include "core/FontManager.h"
#include "core/Outline.h"
#include "editor/MarkdownHighlighter.h"

#include <QKeyEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>

namespace
{

constexpr int kLineNumberPadding = 8;

}

class MarkdownEditor::LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(MarkdownEditor *editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    MarkdownEditor *m_editor; // not owned
};

MarkdownEditor::MarkdownEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_highlighter(new MarkdownHighlighter(document()))
    , m_lineNumberArea(new LineNumberArea(this))
{
    setFont(FontManager().codeFont());
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &MarkdownEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &MarkdownEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &MarkdownEditor::applyExtraSelections);

    updateLineNumberAreaWidth();
    applyExtraSelections();
}

void MarkdownEditor::setTheme(const ThemeDefinition &theme)
{
    m_theme = theme;
    m_highlighter->setTheme(theme); // re-highlights
    setPalette(theme.palette);
    applyExtraSelections();
}

void MarkdownEditor::setCodeFont(const QFont &font)
{
    setFont(font);
    updateLineNumberAreaWidth();
}

void MarkdownEditor::setDocumentLimits(const DocumentLimits &limits)
{
    setHighlightingEnabled(limits.editorHighlightEnabled());
}

void MarkdownEditor::setHighlightingEnabled(bool enabled)
{
    if (m_highlightingEnabled == enabled)
        return;

    m_highlightingEnabled = enabled;
    // Detaching stops the incremental per-block passes QSyntaxHighlighter runs
    // while the user types. Colours already applied go away with the next
    // setMarkdown(), which replaces the whole document.
    m_highlighter->setDocument(enabled ? document() : nullptr);
    if (enabled)
        m_highlighter->rehighlight(); // Qt defers this; the text is on screen now
}

void MarkdownEditor::setMarkdown(const QString &markdown)
{
    setPlainText(markdown);
    m_highlighter->rehighlight();
}

int MarkdownEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int maximum = (std::max)(1, blockCount()); maximum >= 10; maximum /= 10)
        ++digits;
    return kLineNumberPadding + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void MarkdownEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);

    const QRect contents = contentsRect();
    m_lineNumberArea->setGeometry(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height());
}

void MarkdownEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void MarkdownEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void MarkdownEditor::applyExtraSelections()
{
    QList<QTextEdit::ExtraSelection> selections;

    QTextEdit::ExtraSelection currentLine;
    currentLine.format.setBackground(m_theme.markdown.currentLine);
    currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
    currentLine.cursor = textCursor();
    currentLine.cursor.clearSelection();
    selections.append(currentLine);

    // Search hits are appended after the current-line tint so that they draw on
    // top of it. The current hit also takes the selection foreground: its tint
    // alone would be invisible while the find bar holds the focus.
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

void MarkdownEditor::setFindMatches(const QVector<FindState::Match> &matches, int currentIndex)
{
    m_findMatches = matches;
    m_findCurrent = currentIndex;
    applyExtraSelections();

    if (currentIndex < 0 || currentIndex >= m_findMatches.size())
        return;

    // Put the caret on the hit, so the user can carry on typing from it. The
    // resulting cursor-position change re-enters applyExtraSelections, which
    // rebuilds the same list: harmless.
    const FindState::Match &match = m_findMatches.at(currentIndex);
    QTextCursor cursor(document());
    cursor.setPosition(match.position);
    cursor.setPosition(match.position + match.length, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    ensureCursorVisible();
}

void MarkdownEditor::clearFindMatches()
{
    if (m_findMatches.isEmpty() && m_findCurrent < 0)
        return;
    m_findMatches.clear();
    m_findCurrent = -1;
    applyExtraSelections();
}

bool MarkdownEditor::gotoHeading(int index)
{
    if (index < 0)
        return false;

    const QVector<int> lines = Outline::headingSourceLines(toPlainText());
    if (index >= lines.size())
        return false;

    const QTextBlock block = document()->findBlockByLineNumber(lines.at(index));
    if (!block.isValid())
        return false;

    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    centerCursor();
    return true;
}

void MarkdownEditor::gotoSourceLine(int line)
{
    if (line < 0)
        return;

    const QTextBlock block = document()->findBlockByLineNumber(line);
    if (!block.isValid())
        return;

    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
}

void MarkdownEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_theme.palette.color(QPalette::Window));

    painter.setPen(m_theme.markdown.weakText);

    const QTextBlock first = firstVisibleBlock();
    QTextBlock block = first;
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top())
            painter
                .drawText(0, top, m_lineNumberArea->width() - kLineNumberPadding / 2, fontMetrics().height(), Qt::AlignRight, QString::number(blockNumber + 1));

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void MarkdownEditor::setWordWrap(bool wrap)
{
    setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

bool MarkdownEditor::wordWrap() const
{
    return lineWrapMode() != QPlainTextEdit::NoWrap;
}

bool MarkdownEditor::toggleTaskCheckboxAtSourceLine(int line)
{
    if (line < 0)
        return false;

    const QTextBlock block = document()->findBlockByLineNumber(line);
    if (!block.isValid())
        return false;

    const QString text = block.text();
    static const QRegularExpression taskRe(QStringLiteral(R"(^(\s*[-*+]\s+\[)([ xX])(\]\s*.*)$)"));
    const QRegularExpressionMatch match = taskRe.match(text);
    if (!match.hasMatch())
        return false;

    const int checkPos = int(match.capturedStart(2));
    const QString currentMark = match.captured(2);
    const QString newMark = (currentMark == QStringLiteral(" ")) ? QStringLiteral("x") : QStringLiteral(" ");

    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, checkPos);
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
    cursor.insertText(newMark);
    return true;
}

void MarkdownEditor::keyPressEvent(QKeyEvent *event)
{
    if (handleFormatShortcut(event))
        return;
    if (handleReturnKey(event))
        return;
    if (handleTabKey(event))
        return;
    if (handleBackspaceKey(event))
        return;
    if (handleAutoPair(event))
        return;

    QPlainTextEdit::keyPressEvent(event);
}

bool MarkdownEditor::handleReturnKey(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter)
        return false;
    if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::KeypadModifier)
        return false;

    QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const QString text = block.text();
    const int posInBlock = cursor.positionInBlock();
    const QString beforeCursor = text.left(posInBlock);

    static const QRegularExpression taskRe(QStringLiteral(R"(^(\s*)([-*+]\s+\[[ xX]\]\s*)(.*)$)"));
    static const QRegularExpression orderedRe(QStringLiteral(R"(^(\s*)(\d+)([.)]\s*)(.*)$)"));
    static const QRegularExpression bulletRe(QStringLiteral(R"(^(\s*)([-*+]\s+)(.*)$)"));
    static const QRegularExpression quoteRe(QStringLiteral(R"(^(\s*>+\s*)(.*)$)"));
    static const QRegularExpression indentRe(QStringLiteral(R"(^(\s+))"));

    QRegularExpressionMatch match = taskRe.match(beforeCursor);
    if (match.hasMatch()) {
        const QString indent = match.captured(1);
        const QString rest = match.captured(3).trimmed();
        if (rest.isEmpty() && text.trimmed() == beforeCursor.trimmed()) {
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.insertText(indent);
            cursor.endEditBlock();
            return true;
        }
        const QString markerFull = match.captured(2);
        const QString bulletType = markerFull.left(2);
        cursor.insertText(QStringLiteral("\n") + indent + bulletType + QStringLiteral("[ ] "));
        return true;
    }

    match = orderedRe.match(beforeCursor);
    if (match.hasMatch()) {
        const QString indent = match.captured(1);
        const int num = match.captured(2).toInt();
        const QString delim = match.captured(3);
        const QString rest = match.captured(4).trimmed();
        if (rest.isEmpty() && text.trimmed() == beforeCursor.trimmed()) {
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.insertText(indent);
            cursor.endEditBlock();
            return true;
        }
        cursor.insertText(QStringLiteral("\n") + indent + QString::number(num + 1) + delim);
        return true;
    }

    match = bulletRe.match(beforeCursor);
    if (match.hasMatch()) {
        const QString indent = match.captured(1);
        const QString marker = match.captured(2);
        const QString rest = match.captured(3).trimmed();
        if (rest.isEmpty() && text.trimmed() == beforeCursor.trimmed()) {
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.insertText(indent);
            cursor.endEditBlock();
            return true;
        }
        cursor.insertText(QStringLiteral("\n") + indent + marker);
        return true;
    }

    match = quoteRe.match(beforeCursor);
    if (match.hasMatch()) {
        const QString quotePrefix = match.captured(1);
        const QString rest = match.captured(2).trimmed();
        if (rest.isEmpty() && text.trimmed() == beforeCursor.trimmed()) {
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.endEditBlock();
            return true;
        }
        cursor.insertText(QStringLiteral("\n") + quotePrefix);
        return true;
    }

    match = indentRe.match(beforeCursor);
    if (match.hasMatch()) {
        cursor.insertText(QStringLiteral("\n") + match.captured(1));
        return true;
    }

    return false;
}

bool MarkdownEditor::handleTabKey(QKeyEvent *event)
{
    const bool isTab = event->key() == Qt::Key_Tab;
    const bool isBacktab = event->key() == Qt::Key_Backtab || (isTab && (event->modifiers() & Qt::ShiftModifier));
    if (!isTab && !isBacktab)
        return false;

    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        cursor.beginEditBlock();
        const int start = cursor.selectionStart();
        const int end = cursor.selectionEnd();
        QTextBlock startBlock = document()->findBlock(start);
        QTextBlock endBlock = document()->findBlock(end);

        if (end > start && end == endBlock.position())
            endBlock = endBlock.previous();

        QTextBlock block = startBlock;
        while (block.isValid()) {
            QTextCursor blockCursor(block);
            if (isBacktab) {
                const QString blockText = block.text();
                int removeCount = 0;
                if (blockText.startsWith(QLatin1Char('\t'))) {
                    removeCount = 1;
                } else {
                    while (removeCount < 4 && removeCount < blockText.size() && blockText.at(removeCount) == QLatin1Char(' '))
                        ++removeCount;
                }
                if (removeCount > 0) {
                    blockCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, removeCount);
                    blockCursor.removeSelectedText();
                }
            } else {
                blockCursor.insertText(QStringLiteral("    "));
            }
            if (block == endBlock)
                break;
            block = block.next();
        }
        cursor.endEditBlock();
        return true;
    }

    if (isTab) {
        cursor.insertText(QStringLiteral("    "));
        return true;
    }
    return false;
}

bool MarkdownEditor::handleAutoPair(QKeyEvent *event)
{
    const QString text = event->text();
    if (text.isEmpty())
        return false;

    const QChar ch = text.at(0);
    QChar openChar;
    QChar closeChar;

    if (ch == QLatin1Char('(')) {
        openChar = '(';
        closeChar = ')';
    } else if (ch == QLatin1Char('[')) {
        openChar = '[';
        closeChar = ']';
    } else if (ch == QLatin1Char('{')) {
        openChar = '{';
        closeChar = '}';
    } else if (ch == QLatin1Char(')')) {
        openChar = '(';
        closeChar = ')';
    } else if (ch == QLatin1Char(']')) {
        openChar = '[';
        closeChar = ']';
    } else if (ch == QLatin1Char('}')) {
        openChar = '{';
        closeChar = '}';
    } else {
        return false;
    }

    if (ch == closeChar) {
        QTextCursor cursor = textCursor();
        if (!cursor.hasSelection() && !cursor.atBlockEnd()) {
            const QTextBlock block = cursor.block();
            const int posInBlock = cursor.positionInBlock();
            if (posInBlock < block.text().size() && block.text().at(posInBlock) == ch) {
                cursor.movePosition(QTextCursor::Right);
                setTextCursor(cursor);
                return true;
            }
        }
        if (ch != openChar)
            return false;
    }

    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        const QString selected = cursor.selectedText();
        cursor.insertText(QString(openChar) + selected + QString(closeChar));
        return true;
    }

    if (ch == openChar) {
        cursor.insertText(QString(openChar) + QString(closeChar));
        cursor.movePosition(QTextCursor::Left);
        setTextCursor(cursor);
        return true;
    }

    return false;
}

bool MarkdownEditor::handleBackspaceKey(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Backspace || event->modifiers() != Qt::NoModifier)
        return false;

    QTextCursor cursor = textCursor();
    if (cursor.hasSelection())
        return false;

    const QTextBlock block = cursor.block();
    const int pos = cursor.positionInBlock();
    const QString text = block.text();
    if (pos > 0 && pos < text.size()) {
        const QChar before = text.at(pos - 1);
        const QChar after = text.at(pos);
        if ((before == '(' && after == ')') || (before == '[' && after == ']') || (before == '{' && after == '}')) {
            cursor.beginEditBlock();
            cursor.deletePreviousChar();
            cursor.deleteChar();
            cursor.endEditBlock();
            return true;
        }
    }
    return false;
}

bool MarkdownEditor::handleFormatShortcut(QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers();
    const int key = event->key();

    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier) && key == Qt::Key_I) {
        QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) {
            const QString sel = cursor.selectedText();
            if (sel.startsWith(QLatin1Char('*')) && sel.endsWith(QLatin1Char('*')) && sel.size() >= 2) {
                cursor.insertText(sel.mid(1, sel.size() - 2));
            } else {
                cursor.insertText(QStringLiteral("*") + sel + QStringLiteral("*"));
            }
        } else {
            cursor.insertText(QStringLiteral("**"));
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);
        }
        return true;
    }

    if ((mods & Qt::ControlModifier) && !(mods & Qt::ShiftModifier) && key == Qt::Key_K) {
        QTextCursor cursor = textCursor();
        const QString sel = cursor.selectedText();
        if (sel.isEmpty()) {
            cursor.insertText(QStringLiteral("[](url)"));
            cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 6);
        } else {
            cursor.insertText(QStringLiteral("[%1](url)").arg(sel));
        }
        setTextCursor(cursor);
        return true;
    }

    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && key == Qt::Key_C) {
        QTextCursor cursor = textCursor();
        const QString sel = cursor.selectedText();
        if (sel.isEmpty()) {
            cursor.insertText(QStringLiteral("```\n\n```"));
            cursor.movePosition(QTextCursor::PreviousBlock);
        } else {
            cursor.insertText(QStringLiteral("```\n%1\n```\n").arg(sel));
        }
        setTextCursor(cursor);
        return true;
    }

    if ((mods & Qt::ControlModifier) && key >= Qt::Key_1 && key <= Qt::Key_6) {
        toggleHeadingLevel(key - Qt::Key_0);
        return true;
    }

    if (mods == Qt::AltModifier && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        moveCurrentLines(key == Qt::Key_Up ? -1 : 1);
        return true;
    }

    return false;
}

void MarkdownEditor::moveCurrentLines(int direction)
{
    QTextCursor cursor = textCursor();
    QTextBlock block = cursor.block();
    if (direction < 0 && !block.previous().isValid())
        return;
    if (direction > 0 && !block.next().isValid())
        return;

    cursor.beginEditBlock();
    const QString currentText = block.text();
    QTextBlock targetBlock = direction < 0 ? block.previous() : block.next();
    const QString targetText = targetBlock.text();

    QTextCursor blockCursor(block);
    blockCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    blockCursor.insertText(targetText);

    QTextCursor targetCursor(targetBlock);
    targetCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    targetCursor.insertText(currentText);

    cursor.endEditBlock();

    QTextCursor newCursor(targetBlock);
    setTextCursor(newCursor);
}

void MarkdownEditor::toggleHeadingLevel(int level)
{
    if (level < 1 || level > 6)
        return;

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    QTextBlock block = cursor.block();
    QString text = block.text();

    static const QRegularExpression headingRe(QStringLiteral(R"(^(#{1,6})\s*)"));
    const QRegularExpressionMatch match = headingRe.match(text);
    const QString prefix = QString(level, QLatin1Char('#')) + QStringLiteral(" ");

    QTextCursor blockCursor(block);
    if (match.hasMatch()) {
        const int oldLevel = int(match.captured(1).length());
        blockCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, int(match.capturedLength()));
        if (oldLevel == level)
            blockCursor.removeSelectedText();
        else
            blockCursor.insertText(prefix);
    } else {
        blockCursor.insertText(prefix);
    }
    cursor.endEditBlock();
}
