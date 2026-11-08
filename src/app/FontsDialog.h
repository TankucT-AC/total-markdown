// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_APP_FONTSDIALOG_H
#define TOTALMARKDOWN_APP_FONTSDIALOG_H

#include <QDialog>

class QFontComboBox;

/*
 * Lets the reader pick the fonts the app typesets with, out of the families
 * installed on this machine. Two picks: the text face for prose and headings,
 * and a monospace face for code blocks and the source editor. A button puts
 * the bundled faces (Inter, JetBrains Mono) back into the combos; the window
 * applies and remembers the choice on accept (see MainWindow::applyFontChoice).
 */
class FontsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FontsDialog(QWidget *parent = nullptr);

    void setBodyFamily(const QString &family);
    void setCodeFamily(const QString &family);
    QString bodyFamily() const;
    QString codeFamily() const;

private:
    QFontComboBox *m_body = nullptr;
    QFontComboBox *m_code = nullptr;
};

#endif // TOTALMARKDOWN_APP_FONTSDIALOG_H
