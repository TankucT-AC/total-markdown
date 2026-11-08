// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_VIEWER_MATHOBJECTHANDLER_H
#define TOTALMARKDOWN_VIEWER_MATHOBJECTHANDLER_H

#include <QColor>
#include <QObject>
#include <QTextObjectInterface>

class QFont;
class QPainter;
class QTextDocument;

/*
 * Draws LaTeX formulas inside a QTextDocument through the QTextObjectInterface
 * mechanism: the document carries the TeX source, the handler renders it.
 *
 * Bitmaps come from MathRenderer and are cached there. A formula the renderer
 * refuses (a TeX error, a glyph the backend could not produce) degrades to its
 * source text, so a defect in the math pipeline costs readability, not content.
 *
 * Formulas are typeset at the body text size and in the body text colour, and
 * follow the screen's device pixel ratio; all three arrive from the view when
 * the render style changes.
 */
class MathObjectHandler : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    explicit MathObjectHandler(QObject *parent = nullptr);

    void setBodyFont(const QFont &font);
    // Em size the formulas are typeset at, in device-independent pixels -- the
    // unit the document layout works in. Derived from the body font by the view.
    void setEmSize(qreal pixels);
    void setColor(const QColor &color);
    void setDevicePixelRatio(qreal ratio);

    QSizeF intrinsicSize(QTextDocument *document, int positionInDocument, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *document, int positionInDocument, const QTextFormat &format) override;

private:
    QImage bitmapFor(const QTextFormat &format) const;
    QString sourceOf(const QTextFormat &format) const;
    bool isDisplay(const QTextFormat &format) const;
    QSizeF sourceTextSize(const QString &source) const;

    QFont m_bodyFont;
    QColor m_color;
    qreal m_devicePixelRatio = 1.0;
    // 12pt at 96dpi until the view says otherwise.
    qreal m_emSize = 16.0;
};

#endif // TOTALMARKDOWN_VIEWER_MATHOBJECTHANDLER_H
