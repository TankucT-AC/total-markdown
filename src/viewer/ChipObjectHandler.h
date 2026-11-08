// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_VIEWER_CHIPOBJECTHANDLER_H
#define TOTALMARKDOWN_VIEWER_CHIPOBJECTHANDLER_H

#include <QColor>
#include <QImage>
#include <QObject>
#include <QTextObjectInterface>

/*
 * Draws inline "chips" inside the preview — small rounded labels used for the
 * alt text of remote images (e.g. shields.io badges) and for <kbd> keys.
 *
 * Remote images are not fetched: a chip keeps the markdown readable offline,
 * instantly and without touching the network.
 */
class ChipObjectHandler : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    explicit ChipObjectHandler(QObject *parent = nullptr);

    void setColors(const QColor &background, const QColor &border, const QColor &text);

    // Renders one chip into an image, for the placeholder shown when a remote
    // image cannot be downloaded. Uses the same colours and metrics as the
    // on-screen chip so the fallback matches.
    QImage renderChip(const QString &text, qreal pointSize) const;

    QSizeF intrinsicSize(QTextDocument *document, int posInDocument, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *document, int posInDocument, const QTextFormat &format) override;

private:
    void paintChip(QPainter *painter, const QRectF &rect, const QString &text, const QFont &font) const;

    QColor m_background;
    QColor m_border;
    QColor m_text;
};

#endif // TOTALMARKDOWN_VIEWER_CHIPOBJECTHANDLER_H
