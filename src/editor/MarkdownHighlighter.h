// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_EDITOR_MARKDOWNHIGHLIGHTER_H
#define TOTALMARKDOWN_EDITOR_MARKDOWNHIGHLIGHTER_H

#include "core/Theme.h"

#include <QRegularExpression>
#include <QSyntaxHighlighter>

class QTextDocument;

/*
 * Syntax highlighting for the markdown SOURCE shown in the editor pane.
 * This is intentionally lightweight: it highlights structure a writer needs
 * to see while typing (headings, fences, spans, links, markers), not the
 * full CommonMark grammar. Fenced code blocks use a block-state machine so
 * multi-line fences are styled correctly.
 */
class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setTheme(const ThemeDefinition &theme);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum BlockState : int {
        StateNormal = 0,
        StateInFence = 1,
    };

    void applyLineRules(const QString &text);
    void applySpanRules(const QString &text);
    void setFenceLineFormat(const QString &text);
    QTextCharFormat makeFormat(QColor color, QFont::Weight weight = QFont::Normal) const;

    ThemeDefinition m_theme;
    QRegularExpression m_fenceLine;
    QRegularExpression m_heading;
    QRegularExpression m_bold;
    QRegularExpression m_emphasis;
    QRegularExpression m_inlineCode;
    QRegularExpression m_image;
    QRegularExpression m_link;
    QRegularExpression m_quoteMarker;
    QRegularExpression m_taskMarker;
    QRegularExpression m_listMarker;
    QRegularExpression m_horizontalRule;
};

#endif // TOTALMARKDOWN_EDITOR_MARKDOWNHIGHLIGHTER_H
