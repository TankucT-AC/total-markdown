// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Outline.h"

#include <QHash>
#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

/*
 * The table of contents: one row per heading, nested by level, following the
 * preview as the reader scrolls. Choosing a row asks the window to go there —
 * the panel never scrolls anything itself.
 */
class OutlinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget *parent = nullptr);

    void setOutline(const QVector<OutlineEntry> &entries);

    // Marks the section the reader is under. Unknown block numbers clear the
    // selection instead of guessing.
    void setCurrentBlock(int blockNumber);

    int entryCount() const
    {
        return int(m_entries.size());
    }

signals:
    void entryActivated(int index); // index into the outline last handed over

private:
    void activate(QTreeWidgetItem *item);
    void rebuildTree();
    void filterOutline(const QString &query);

    QLineEdit *m_filter = nullptr;
    QTreeWidget *m_tree = nullptr;
    QVector<OutlineEntry> m_entries;
    QHash<int, QTreeWidgetItem *> m_items; // outline index -> row
};
