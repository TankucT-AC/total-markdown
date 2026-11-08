// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_MARKDOWNBUILDER_H
#define TOTALMARKDOWN_CORE_MARKDOWNBUILDER_H

#include "core/FontManager.h"
#include "core/Theme.h"

#include <QTextFormat>

class QString;
class QTextDocument;

/*
 * Everything the markdown renderer needs to know about looks: colors, fonts
 * and the base directory for resolving relative image references.
 */
struct RenderStyle {
    ThemeDefinition theme;
    FontManager fonts;
    QString baseDir;
    int maxImageWidth = 760;
    // Maximum width of the rendered column; wider panes centre the content,
    // like Zed, instead of stretching text across the whole window.
    int maxContentWidth = 820;
    // The text column the view actually has right now, used to resolve
    // percentage image widths (a `width="100%"` banner). Zero means "no live
    // width known", in which case maxContentWidth is used instead.
    int contentWidth = 0;
    // When true, http(s) image references render as images the viewer loads
    // over the network; when false they fall back to the alt-text chip, which
    // is also the behaviour for an image whose download failed.
    bool remoteImagesEnabled = false;
};

/*
 * Turns markdown source into a rendered QTextDocument using the md4c parser
 * (the same parser Qt's own markdown import is based on).
 *
 * The builder is a pure function of (source, style): no widgets, no event
 * loop, safe to call from tests. Rebuilding is done in place — clear() plus
 * appending — so the QTextDocument instance and its layout stay valid across
 * incremental re-renders.
 *
 * Blocks are marked with properties so later passes can find them without
 * re-parsing: fenced code blocks carry their language in languageKey, which
 * is what the syntax highlighter keys on.
 */
class MarkdownBuilder
{
public:
    enum class BlockKind {
        None = 0,
        Heading,
        Code,
        Quote,
        Rule,
    };

    // QTextFormat property keys used on block formats and the GIF object.
    enum PropertyKey : int {
        kindKey = QTextFormat::UserProperty + 1,
        headingLevelKey,
        languageKey,
        gifPathKey,
        checkboxCheckedKey,
        checkboxSizeKey,
        chipTextKey,
        chipKindKey,
        mathSourceKey,
        mathDisplayKey,
        // Alt text of a remote image, kept so a failed download can fall back
        // to the same chip the disabled path shows.
        imageAltKey,
        // Source line (0-based) a block was built from; the viewer uses it to
        // send a click in the preview back to the right line in the editor.
        sourceLineKey,
    };

    // QTextCharFormat object type used for animated images. The viewer
    // registers a QTextObjectInterface handler for it.
    static constexpr int gifObjectType = QTextFormat::UserObject + 1;
    // Object type used for task-list checkboxes; the viewer draws them.
    static constexpr int checkboxObjectType = QTextFormat::UserObject + 2;
    // Object type used for inline "chips": remote-image badges and <kbd> keys.
    static constexpr int chipObjectType = QTextFormat::UserObject + 3;
    // Object type used for LaTeX formulas ($...$ and $$...$$); the viewer
    // draws them through MathRenderer.
    static constexpr int mathObjectType = QTextFormat::UserObject + 4;

    // Kind of an inline chip, stored under chipKindKey.
    enum class ChipKind : int {
        Badge, // a remote <img> rendered as its alt text
        Key, // a <kbd> keycap
    };

    // Parses markdown and rebuilds target as a rendered document.
    // Returns false only when the parser itself fails.
    static bool build(QTextDocument *target, const QString &markdown, const RenderStyle &style);

    // GitHub-style heading anchor: lowercased, spaces to dashes, punctuation
    // dropped. Used to resolve in-document links like "#getting-started".
    static QString headingSlug(const QString &heading);
};

#endif // TOTALMARKDOWN_CORE_MARKDOWNBUILDER_H
