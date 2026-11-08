// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_VIEWER_CODEHIGHLIGHTER_H
#define TOTALMARKDOWN_VIEWER_CODEHIGHLIGHTER_H

#include "core/Theme.h"

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <QTextBlock>

class QTextDocument;

/*
 * Applies KF6 syntax highlighting to the fenced code blocks of a document
 * produced by MarkdownBuilder.
 *
 * The document mixes languages (one fence may be C++, the next Python), so
 * this cannot be a document-wide QSyntaxHighlighter: each fenced region is
 * highlighted on its own, with the state machine reset at its first line.
 * The language comes from the block's languageKey property, set by the
 * builder from the info string of the fence.
 */
class CodeHighlighter : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    // Resolves the syntax theme named in the ThemeDefinition and re-tints
    // every code block on the next highlight() call.
    void setThemeDefinition(const ThemeDefinition &theme);

    // Caps the work a single pass is allowed to do, in QString characters.
    // -1 means "no limit" and is the default: for a document of ordinary size
    // there is nothing to gain from a ceiling, but one pathological block can
    // otherwise stall the window.
    void setLimits(int maxBlockChars, int maxTotalChars);

    void highlight(QTextDocument *document);

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override;

private:
    KSyntaxHighlighting::Definition definitionFor(const QString &language) const;

    // Maps a KSyntaxHighlighting text style onto the theme's code palette.
    // Returns an invalid color for roles that should keep the block's own text
    // color.
    QColor syntaxColor(KSyntaxHighlighting::Theme::TextStyle style) const;

    KSyntaxHighlighting::Repository m_repository;
    ThemeDefinition m_themeDefinition;
    QTextBlock m_block; // block currently being highlighted
    int m_maxBlockChars = -1;
    int m_maxTotalChars = -1;
};

#endif // TOTALMARKDOWN_VIEWER_CODEHIGHLIGHTER_H
