// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_VIEWER_CHECKBOXOBJECTHANDLER_H
#define TOTALMARKDOWN_VIEWER_CHECKBOXOBJECTHANDLER_H

#include <QColor>
#include <QObject>
#include <QTextObjectInterface>

/*
 * Draws task-list checkboxes inside the preview, as a QTextObjectInterface.
 * The old approach used the ☐ / ☑ glyphs, whose shape and colour did not fit
 * the theme; here the box is drawn to match the palette — a rounded outline
 * when unchecked, and an accent-filled box with a tick when checked.
 */
class CheckboxObjectHandler : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    explicit CheckboxObjectHandler(QObject *parent = nullptr);

    void setColors(const QColor &border, const QColor &checkedFill, const QColor &check);

    QSizeF intrinsicSize(QTextDocument *document, int posInDocument, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *document, int posInDocument, const QTextFormat &format) override;

private:
    QColor m_border;
    QColor m_checkedFill;
    QColor m_check;
};

#endif // TOTALMARKDOWN_VIEWER_CHECKBOXOBJECTHANDLER_H
