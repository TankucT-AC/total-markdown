// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_LOCALIMAGECACHE_H
#define TOTALMARKDOWN_CORE_LOCALIMAGECACHE_H

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>
#include <QStringList>

class LocalImageCache
{
public:
    static LocalImageCache &instance();

    // Returns the natural dimensions of the image at path. If cached and the
    // file modification timestamp has not changed, avoids any disk I/O.
    QSize naturalSize(const QString &path);

    // Returns a scaled copy of the image at path for display at pixelSize and
    // devicePixelRatio. Results are cached in memory so subsequent renders
    // (typing, resizing, scrolling) are instantaneous (0 ms).
    QImage getScaled(const QString &path, const QSize &pixelSize, qreal ratio, const QSize &naturalSize);

    // Asynchronously pre-decodes the given list of local file paths on worker
    // threads in QThreadPool, pre-warming the cache before renderNow() runs.
    void prefetch(const QStringList &paths);

    // Clears all in-memory caches.
    void clear();

private:
    LocalImageCache() = default;
    ~LocalImageCache() = default;
    LocalImageCache(const LocalImageCache &) = delete;
    LocalImageCache &operator=(const LocalImageCache &) = delete;

    struct RawEntry {
        QImage image;
        QSize naturalSize;
        qint64 lastModified = 0;
    };

    struct ScaledEntry {
        QImage image;
        qint64 lastModified = 0;
    };

    QImage loadRawUnlocked(const QString &path, qint64 mtime);

    mutable QMutex m_mutex;
    QHash<QString, RawEntry> m_rawCache;
    QHash<QString, ScaledEntry> m_scaledCache;
};

#endif // TOTALMARKDOWN_CORE_LOCALIMAGECACHE_H
