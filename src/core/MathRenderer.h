// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_MATHRENDERER_H
#define TOTALMARKDOWN_CORE_MATHRENDERER_H

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>

/*
 * LaTeX formulas to bitmaps, with a cache.
 *
 * MicroTeX keeps global font tables and is not documented as thread-safe, so
 * every call goes through one instance per process; the cache absorbs the
 * repeats, which matters because the preview rebuilds on every keystroke while
 * the formulas themselves have not changed.
 *
 * The colour is baked into the bitmap, so switching theme has to invalidate the
 * cache — MarkdownView does that when the render style changes.
 */
class MathRenderer
{
public:
    // The one renderer. Initialising MicroTeX twice is not supported, and its
    // resource tables are global anyway.
    static MathRenderer &instance();

    MathRenderer(const MathRenderer &) = delete;
    MathRenderer &operator=(const MathRenderer &) = delete;

    // Locates the TeX resources and brings MicroTeX up. Returns false when they
    // are not installed, in which case formulas stay as their source text.
    bool isAvailable();

    // Widest formula the layout may produce, in pixels. Display formulas wrap
    // inside it; inline ones are given room to stay on a single line.
    void setMaximumWidth(int width);

    // The bitmap for `tex`, or a null image when the source does not parse.
    // The result is in device pixels and carries its devicePixelRatio, so the
    // layout can use logical pixels.
    QImage image(const QString &tex, bool display, qreal pointSize, const QColor &color, qreal devicePixelRatio);

    void clearCache();
    int cacheSize() const
    {
        return int(m_cache.size());
    }

    // Where the TeX resources were found, or empty when they are missing.
    static QString resourceDirectory();

private:
    MathRenderer() = default;
    ~MathRenderer();

    QString keyFor(const QString &tex, bool display, qreal pointSize, const QColor &color, qreal devicePixelRatio) const;

    bool m_initialised = false;
    bool m_available = false;
    int m_maximumWidth = 1024;
    QHash<QString, QImage> m_cache;
};

#endif // TOTALMARKDOWN_CORE_MATHRENDERER_H
