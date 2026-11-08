// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_IMAGERESOLVER_H
#define TOTALMARKDOWN_CORE_IMAGERESOLVER_H

#include <QString>

/*
 * Resolves image references found in markdown to concrete file paths,
 * relative to the directory of the markdown file (as most renderers do).
 */
class ImageResolver
{
public:
    // Returns an absolute local path, or an empty string when the reference
    // cannot be resolved to a local file. Remote http(s) URLs resolve to an
    // empty string here — the viewer loads them over the network instead.
    static QString resolve(const QString &source, const QString &baseDir);

    // True for http(s) references; those travel through the network fetcher
    // rather than the local file system.
    static bool isRemote(const QString &source);

    // True when the path points to an animated image format the viewer can
    // play (GIF in v1).
    static bool isAnimated(const QString &path);
};

#endif // TOTALMARKDOWN_CORE_IMAGERESOLVER_H
