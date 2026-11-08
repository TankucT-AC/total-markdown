// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/DocumentLimits.h"
#include "core/FindState.h"
#include "core/Theme.h"

#include <QPlainTextEdit>

class MarkdownHighlighter;

/*
 * Source pane of the two-pane view: a plain text editor with line numbers,
 * current-line highlighting and markdown syntax highlighting. Editing is the
 * input side of the live preview; the editor itself knows nothing about
 * rendering markdown.
 */
class MarkdownEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    void setTheme(const ThemeDefinition &theme);

    // The source editor's face. Also recomputes the line-number gutter, which
    // measures itself against the font metrics.
    void setCodeFont(const QFont &font);

    // Applies the document-size policy. Above a threshold the markdown
    // highlighter is detached: re-highlighting a huge file on every keystroke
    // costs far more than the colours are worth.
    void setDocumentLimits(const DocumentLimits &limits);

    void setHighlightingEnabled(bool enabled);
    bool highlightingEnabled() const
    {
        return m_highlightingEnabled;
    }

    // Search decorations. The editor owns them because Qt allows only a single
    // ExtraSelections list: the current-line tint and the search hits have to be
    // composed into one call. The caret follows the current hit, so the user can
    // keep typing from there.
    void setFindMatches(const QVector<FindState::Match> &matches, int currentIndex);
    void clearFindMatches();

    // Moves the caret to the index-th ATX heading line of the source and selects
    // it. Returns false when the source has no such heading — which is how a
    // caller finds out that the renderer and the scan disagree.
    bool gotoHeading(int index);

    // Puts the caret on a zero-based source line, without taking the focus: the
    // preview stays usable while the editor follows it (click-to-source).
    void gotoSourceLine(int line);

    // Replaces the whole document and re-highlights. Programmatic text
    // replacement needs an explicit rehighlight: the synchronous pass Qt runs
    // during setPlainText writes to a layout that is discarded afterwards.
    void setMarkdown(const QString &markdown);

    void setWordWrap(bool wrap);
    bool wordWrap() const;

    // Toggles a task checkbox `- [ ]` <-> `- [x]` on the given 0-based source line.
    bool toggleTaskCheckboxAtSourceLine(int line);

    int lineNumberAreaWidth() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void applyExtraSelections();

private:
    class LineNumberArea;

    bool handleReturnKey(QKeyEvent *event);
    bool handleTabKey(QKeyEvent *event);
    bool handleBackspaceKey(QKeyEvent *event);
    bool handleAutoPair(QKeyEvent *event);
    bool handleFormatShortcut(QKeyEvent *event);
    void moveCurrentLines(int direction);
    void toggleHeadingLevel(int level);

    void lineNumberAreaPaintEvent(QPaintEvent *event);

    MarkdownHighlighter *m_highlighter = nullptr;
    LineNumberArea *m_lineNumberArea = nullptr;
    ThemeDefinition m_theme;
    bool m_highlightingEnabled = true;
    QVector<FindState::Match> m_findMatches;
    int m_findCurrent = -1;
};
