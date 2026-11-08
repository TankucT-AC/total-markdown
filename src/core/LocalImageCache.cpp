// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/LocalImageCache.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>

namespace
{

constexpr int kMaxRawCacheEntries = 64;
constexpr int kMaxScaledCacheEntries = 128;

qint64 fileModificationMsecs(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return -1;
    return info.lastModified().toMSecsSinceEpoch();
}

QString scaledKey(const QString &path, const QSize &pixelSize, qreal ratio)
{
    return path + QLatin1Char('@') + QString::number(pixelSize.width()) + QLatin1Char('x') + QString::number(pixelSize.height()) + QLatin1Char('_')
        + QString::number(ratio);
}

} // namespace

LocalImageCache &LocalImageCache::instance()
{
    static LocalImageCache s_instance;
    return s_instance;
}

QSize LocalImageCache::naturalSize(const QString &path)
{
    if (path.isEmpty())
        return QSize();

    const qint64 mtime = fileModificationMsecs(path);
    if (mtime < 0)
        return QSize();

    QMutexLocker locker(&m_mutex);
    const auto it = m_rawCache.constFind(path);
    if (it != m_rawCache.constEnd() && it->lastModified == mtime && it->naturalSize.isValid())
        return it->naturalSize;

    // Fast-path: query header dimensions without decoding the whole bitmap.
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.isValid()) {
        m_rawCache[path] = RawEntry{QImage(), size, mtime};
        return size;
    }

    // Fall back to full load if reader couldn't get size from header.
    const QImage img = loadRawUnlocked(path, mtime);
    return img.size();
}

QImage LocalImageCache::loadRawUnlocked(const QString &path, qint64 mtime)
{
    const auto it = m_rawCache.constFind(path);
    if (it != m_rawCache.constEnd() && it->lastModified == mtime && !it->image.isNull())
        return it->image;

    QImage loaded(path);
    if (!loaded.isNull()) {
        if (m_rawCache.size() >= kMaxRawCacheEntries)
            m_rawCache.clear();
        m_rawCache[path] = RawEntry{loaded, loaded.size(), mtime};
    }
    return loaded;
}

QImage LocalImageCache::getScaled(const QString &path, const QSize &pixelSize, qreal ratio, const QSize &naturalSize)
{
    if (path.isEmpty() || !pixelSize.isValid())
        return QImage();

    const qint64 mtime = fileModificationMsecs(path);
    if (mtime < 0)
        return QImage();

    const QString key = scaledKey(path, pixelSize, ratio);

    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_scaledCache.constFind(key);
        if (it != m_scaledCache.constEnd() && it->lastModified == mtime && !it->image.isNull())
            return it->image;
    }

    // Retrieve or load the unscaled image.
    QImage raw;
    {
        QMutexLocker locker(&m_mutex);
        raw = loadRawUnlocked(path, mtime);
    }

    if (raw.isNull())
        return QImage();

    QImage scaled;
    const QSize effectiveNatural = naturalSize.isValid() ? naturalSize : raw.size();

    // Fast 2-step downsampling when scaling down by more than 2.5x:
    // Box-sample down to 2x target resolution (instant nearest-neighbor reduction),
    // then apply high-quality smooth antialiasing to the small intermediate bitmap.
    if (effectiveNatural.width() > pixelSize.width() * 5 / 2 && effectiveNatural.height() > pixelSize.height() * 5 / 2) {
        const QSize intermediate = pixelSize * 2;
        const QImage half = raw.scaled(intermediate, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        scaled = half.scaled(pixelSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    } else if (raw.size() == pixelSize) {
        scaled = raw;
    } else {
        scaled = raw.scaled(pixelSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    if (!scaled.isNull())
        scaled.setDevicePixelRatio(ratio);

    {
        QMutexLocker locker(&m_mutex);
        if (m_scaledCache.size() >= kMaxScaledCacheEntries)
            m_scaledCache.clear();
        m_scaledCache[key] = ScaledEntry{scaled, mtime};
    }

    return scaled;
}

void LocalImageCache::prefetch(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;

        const qint64 mtime = fileModificationMsecs(path);
        if (mtime < 0)
            continue;

        {
            QMutexLocker locker(&m_mutex);
            const auto it = m_rawCache.constFind(path);
            if (it != m_rawCache.constEnd() && it->lastModified == mtime && !it->image.isNull())
                continue; // already in memory
        }

        QThreadPool::globalInstance()->start(QRunnable::create([this, path, mtime]() {
            QImage loaded(path);
            if (!loaded.isNull()) {
                QMutexLocker locker(&m_mutex);
                if (m_rawCache.size() >= kMaxRawCacheEntries)
                    m_rawCache.clear();
                m_rawCache[path] = RawEntry{loaded, loaded.size(), mtime};
            }
        }));
    }
}

void LocalImageCache::clear()
{
    QMutexLocker locker(&m_mutex);
    m_rawCache.clear();
    m_scaledCache.clear();
}
