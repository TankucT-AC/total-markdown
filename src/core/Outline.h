// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_OUTLINE_H
#define TOTALMARKDOWN_CORE_OUTLINE_H

#include <QString>
#include <QVector>

class QTextDocument;

/*
 * One heading of a rendered document, as the outline panel needs it.
 */
struct OutlineEntry {
    int level = 1; // 1 (H1) .. 6 (H6)
    QString title;
    QString slug; // GitHub-style anchor, matching MarkdownBuilder::headingSlug
    int blockNumber = 0; // where the heading sits in the rendered document
};

/*
 * The heading structure of a document.
 *
 * The rendered document already knows everything: MarkdownBuilder marks heading
 * blocks and stores their level, so building an outline is one walk and no
 * second parse. It follows that the outline can never disagree with what the
 * preview shows.
 */
class Outline
{
public:
    static QVector<OutlineEntry> fromDocument(const QTextDocument &document);

    // Zero-based source lines of the ATX headings, in document order, skipping
    // fenced code. The editor uses it to follow a click in the panel.
    //
    // The renderer also understands Setext headings and HTML, so the two lists
    // can disagree on exotic documents: callers must compare the sizes before
    // trusting the pairing.
    static QVector<int> headingSourceLines(const QString &markdown);
};

#endif // TOTALMARKDOWN_CORE_OUTLINE_H
