// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_FONTMANAGER_H
#define TOTALMARKDOWN_CORE_FONTMANAGER_H

#include <QFont>
#include <QStringList>

/*
 * Central typography for the whole application: markdown prose, headings and
 * code, with three interchangeable sets.
 *
 * - Bundled: Inter (OFL) for prose and UI, JetBrains Mono (OFL) for code,
 *   compiled into the executable as Qt resources, so the look is identical on
 *   every machine.
 * - System: the platform's proportional and fixed fonts.
 * - Custom: concrete families picked from the ones installed on this machine.
 *   A blank member falls back to its bundled face, so a choice can be partial.
 *
 * Heading sizes follow a modular type scale (base 12 pt, ratio 1.2), so the
 * hierarchy stays proportional when the base changes.
 */
class FontManager
{
public:
    enum class FontSet {
        Bundled,
        System,
        Custom,
    };

    // Registers the bundled OFL fonts with the application font database.
    // Idempotent. Returns the families that are available afterwards.
    static QStringList ensureBundledFontsLoaded();

    explicit FontManager(FontSet set = FontSet::Bundled);

    void setFontSet(FontSet set);
    FontSet fontSet() const;

    // Chooses the concrete families the Custom set resolves to. Blank members
    // keep the bundled faces.
    void setCustomFonts(const QString &bodyFamily, const QString &codeFamily);
    QString customBodyFamily() const;
    QString customCodeFamily() const;

    QFont bodyFont() const; // markdown prose
    QFont codeFont() const; // fenced blocks, inline code, source editor
    QFont headingFont(int level) const; // 1 (largest) .. 6 (smallest)

    qreal bodyPointSize() const;
    qreal codePointSize() const;
    static qreal headingPointSize(int level);

private:
    QFont proportionalFont(qreal pointSize, QFont::Weight weight) const;

    FontSet m_set = FontSet::Bundled;
    QString m_customBody;
    QString m_customCode;
};

#endif // TOTALMARKDOWN_CORE_FONTMANAGER_H
