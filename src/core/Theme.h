// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_THEME_H
#define TOTALMARKDOWN_CORE_THEME_H

#include <QColor>
#include <QJsonObject>
#include <QPalette>
#include <QString>

#include <optional>

/*
 * Colors used by the markdown renderer that have no counterpart in QPalette.
 * Everything is expressed as semantic roles; widgets and documents must not
 * hardcode literal colors, so a theme swap never requires touching render code.
 */
struct MarkdownColors {
    QColor heading; // H1..H5
    QColor weakText; // H6, meta information
    QColor link;
    QColor codeBackground; // fenced blocks and inline code spans
    QColor codeText;
    QColor quoteBackground;
    QColor quoteBar; // vertical bar left of a quote
    QColor rule; // horizontal rules
    QColor tableBorder;
    QColor tableHeaderBackground;
    QColor taskOpen; // unchecked task marker
    QColor taskDone; // checked task marker
    QColor currentLine; // editor's active-line tint
};

/*
 * Syntax-highlighting roles for code blocks, in the spirit of GitHub's palette.
 * CodeHighlighter maps KSyntaxHighlighting text styles onto these, so code
 * colors match the application instead of a foreign theme.
 */
struct SyntaxColors {
    QColor keyword;
    QColor function;
    QColor variable;
    QColor type; // data types, classes, built-ins
    QColor constant; // numbers and constants
    QColor comment;
    QColor string;
    QColor preprocessor;
    QColor op;
    QColor annotation;
    QColor warning;
    QColor error;
};

/*
 * A complete theme: application palette plus markdown roles plus the name of
 * the KSyntaxHighlighting theme used for fenced code blocks.
 *
 * Built-in themes are constructed with light() / dark(). Custom themes come
 * from JSON files and round-trip through toJson() / fromJson().
 */
struct ThemeDefinition {
    QString name;
    QString syntaxThemeName; // legacy KF6 theme name; code colors come from syntax
    QPalette palette;
    MarkdownColors markdown;
    SyntaxColors syntax;

    static ThemeDefinition light();
    static ThemeDefinition dark();

    QJsonObject toJson() const;
    static std::optional<ThemeDefinition> fromJson(const QJsonObject &object);
};

#endif // TOTALMARKDOWN_CORE_THEME_H
