// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_IMAGEFETCHER_H
#define TOTALMARKDOWN_CORE_IMAGEFETCHER_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QUrl>

class QImage;
class QNetworkAccessManager;

/*
 * Loads remote images over http(s), asynchronously and with guard rails.
 *
 * The fetcher never blocks the caller: the request goes out in the background
 * and the decoded image arrives via ready(). Decoded images are kept in a
 * memory cache, the originals in a disk cache, so a document opened twice or
 * two documents sharing a badge download the bytes once.
 *
 * Security posture: http and https only, private and loopback network space
 * is refused (a hostile markdown file must not be able to probe the local
 * network), redirects are followed by hand and re-checked on every hop, the
 * response is capped in bytes, and the transfer has a timeout.
 */
class ImageFetcher : public QObject
{
    Q_OBJECT

public:
    explicit ImageFetcher(QObject *parent = nullptr);
    ~ImageFetcher() override;

    // When disabled, fetch() does nothing and nothing is downloaded; the
    // viewer shows its own placeholder instead.
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Overrides the disk cache directory (tests point it at a temporary dir);
    // the default is <user cache>/total-markdown/remote-images.
    void setCacheDirectory(const QString &path);

    // Guard rail knobs. Production code keeps the defaults; tests shrink them
    // so failure paths stay fast.
    void setRequestTimeoutMs(int ms);
    void setMaximumBytes(qint64 bytes);
    void setMaximumRedirects(int redirects);
    void setConcurrentRequestLimit(int limit);

    // The SSRF guard refuses loopback and private network space; tests that
    // serve fixtures over loopback turn this off for the fixture host.
    void setPrivateNetworkAllowed(bool allowed);

    // SVG rasters are produced at the intrinsic size times this factor.
    void setDevicePixelRatio(qreal ratio);

    // Synchronous cache read: memory first, then the disk cache. An empty
    // image means "not cached".
    QImage cachedImage(const QUrl &url);

    // Starts a background load. Requests for a URL that is already in flight
    // are coalesced; a URL that already failed stays failed until
    // clearFailed() so relayouts never retry in a loop. Nothing is emitted
    // while disabled.
    void fetch(const QUrl &url);
    bool isFetching(const QUrl &url) const;

    // Forgets past failures so their URLs can be requested again (e.g. when
    // the network comes back).
    void clearFailed();

signals:
    void ready(const QUrl &url, const QImage &image);
    void failed(const QUrl &url);

private:
    struct Request;

    QNetworkAccessManager *m_network;
    bool m_enabled = true;
    int m_timeoutMs = 10000;
    qint64 m_maximumBytes = 4 * 1024 * 1024;
    int m_maximumRedirects = 3;
    int m_concurrentLimit = 4;
    bool m_privateAllowed = false;
    qreal m_devicePixelRatio = 1.0;
    QString m_cacheDirectory;

    QHash<QUrl, QImage> m_memoryCache;
    QSet<QUrl> m_failed;
    QHash<QUrl, Request *> m_requests;
    QVector<QUrl> m_pending;

    void resolve(Request *request, const QUrl &url);
    void startGet(Request *request, const QUrl &url);
    void finish(Request *request);
    void fail(const QUrl &url);
    void startNextPending();
    QImage decode(const QByteArray &bytes, const QString &contentType) const;
    QString cacheDirectory() const;
    QString diskDataPath(const QUrl &url) const;
    QString diskMetaPath(const QUrl &url) const;
    void storeOnDisk(const QUrl &url, const QByteArray &bytes, const QString &contentType);
};

#endif // TOTALMARKDOWN_CORE_IMAGEFETCHER_H
