// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_APP_SIDEBARTITLEBAR_H
#define TOTALMARKDOWN_APP_SIDEBARTITLEBAR_H

#include <QWidget>

class QDockWidget;
class QAbstractButton;

/*
 * The sidebar's own title bar.
 *
 * Qt draws the stock float/close buttons small (about 18x14 in Fusion) and a
 * style sheet cannot enlarge them, so the sidebar carries its own: the title,
 * then two buttons sized to be hit with a pointer. Each button paints its own
 * glyph, the way the code box's copy button does, so it stays sharp on a high-DPI
 * screen instead of being a scaled bitmap.
 *
 * Trade-off: a custom title bar gives up drag-to-redock, which the float button
 * still covers.
 */
class SidebarTitleBar : public QWidget
{
    Q_OBJECT

public:
    SidebarTitleBar(QDockWidget *dock, const QString &title, QWidget *parent = nullptr);

private:
    QAbstractButton *m_float = nullptr;
    QAbstractButton *m_close = nullptr;
};

#endif // TOTALMARKDOWN_APP_SIDEBARTITLEBAR_H
