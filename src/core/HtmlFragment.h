// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_HTMLFRAGMENT_H
#define TOTALMARKDOWN_CORE_HTMLFRAGMENT_H

#include <QString>
#include <QVector>

#include <utility>

/*
 * A tiny, tolerant HTML fragment tokenizer.
 *
 * Qt's XML reader is too strict for the HTML that appears in real markdown
 * (unclosed tags, attributes without quotes, raw text), so we scan the handful
 * of constructs a README actually uses. Malformed input is never fatal: an
 * unterminated tag simply becomes text.
 */
struct HtmlToken {
    enum Kind : int {
        Text, // literal text between tags
        StartTag, // <b>, <div align="center">
        EndTag, // </b>
        SelfClosingTag, // <br/>, <img ... />
        Comment, // <!-- ... -->
        Declaration, // <!DOCTYPE ...>, <![CDATA[ ... ]]>
    };

    Kind kind = Text;
    QString name; // lowercased tag name (empty for Text/Comment/Declaration)
    QVector<std::pair<QString, QString>> attributes; // lowercased attribute names
    QString text; // Text or Comment content
    bool selfClosing = false; // StartTag written as <name/> (HTML void elements)
    // Where the token sits in the source string (one past the last character
    // for `end`). The block renderer slices raw inner HTML out of a container
    // with these, e.g. the markdown inside a <td>.
    int start = -1;
    int end = -1;
};

// Splits a raw HTML fragment into tokens. Never throws; never crashes.
QVector<HtmlToken> parseHtmlFragment(const QString &html);

// The value of an attribute, or an empty string when it is absent.
QString htmlAttribute(const HtmlToken &token, const QString &name);

// True for HTML elements that never have a closing tag (img, br, hr, ...).
bool isHtmlVoidElement(const QString &name);

#endif // TOTALMARKDOWN_CORE_HTMLFRAGMENT_H
