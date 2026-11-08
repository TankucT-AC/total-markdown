// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_VIEWER_GIFOBJECTHANDLER_H
#define TOTALMARKDOWN_VIEWER_GIFOBJECTHANDLER_H

#include <QHash>
#include <QObject>
#include <QTextObjectInterface>

class QMovie;
class QTextDocument;

/*
 * Draws animated images (GIF) inside a QTextDocument via the
 * QTextObjectInterface mechanism — the document counterpart of a widget.
 *
 * Each distinct image has one QMovie driving its frames. When a frame
 * arrives, only the object's own region is invalidated, so playback never
 * forces a full-document relayout.
 */
class GifObjectHandler : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    explicit GifObjectHandler(QTextDocument *document, QObject *parent = nullptr);

    void setMaximumWidth(int width);

    // Retargets the handler when the view swaps in a freshly built document;
    // the movies themselves stay, they are keyed by image path.
    void setDocument(QTextDocument *document);

    // Stops and drops the movies for images no longer referenced by the
    // document. Call after a rebuild so a removed GIF does not animate forever.
    void syncToDocument();

    QSizeF intrinsicSize(QTextDocument *document, int posInDocument, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *document, int posInDocument, const QTextFormat &format) override;

    // True when at least one animated image is currently playing.
    bool hasRunningAnimations() const;

private:
    QMovie *movieFor(const QString &path);
    QPixmap scaledFrame(const QString &path, const QSize &size);

    QTextDocument *m_document;
    int m_maximumWidth = 760;
    QHash<QString, QMovie *> m_movies;
    QHash<QString, int> m_positions;
    QHash<QString, QPixmap> m_scaled;
    QHash<QString, QSize> m_scaledSize;
};

#endif // TOTALMARKDOWN_VIEWER_GIFOBJECTHANDLER_H
