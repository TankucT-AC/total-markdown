// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_APP_FINDCONTROLLER_H
#define TOTALMARKDOWN_APP_FINDCONTROLLER_H

#include "core/FindState.h"

#include <QObject>

class FindBar;
class MarkdownEditor;
class MarkdownView;

/*
 * Wires the two find bars to the two panes. It owns the per-pane search state,
 * decides which pane Ctrl+F applies to, and keeps the "3 of 12" counter honest.
 *
 * The panes only know how to decorate a match; the searching lives in FindState
 * and the deciding lives here.
 */
class FindController : public QObject
{
    Q_OBJECT

public:
    FindController(MarkdownEditor *editor, FindBar *editorBar, MarkdownView *viewer, FindBar *viewerBar, QObject *parent = nullptr);

    // Ctrl+F: the bar of the pane that has the keyboard focus.
    void openForFocusedPane();

    // Ctrl+H: the bar with replace mode opened.
    void openReplaceForFocusedPane();

    // F3 and Shift+F3. Both open the bar when it is closed, so a shortcut the
    // user remembers keeps working.
    void findNext();
    void findPrevious();

    // Drops both searches and hides both bars; used when the document changes
    // underneath them.
    void reset();

private:
    enum class Pane {
        Editor,
        Viewer,
    };

    static Pane otherPane(Pane pane);

    FindState &stateOf(Pane pane);
    const FindState &stateOf(Pane pane) const;
    FindBar *barOf(Pane pane) const;
    QString textOf(Pane pane) const;
    int anchorPositionOf(Pane pane) const;
    bool stale(Pane pane) const;
    void setStale(Pane pane, bool value);
    bool upToDate(Pane pane) const;
    Pane focusedPane() const;

    void show(Pane pane);
    void close(Pane pane);
    void run(Pane pane, const QString &query, bool caseSensitive);
    void step(Pane pane, int direction);
    void replaceCurrent(Pane pane, const QString &replacement);
    void replaceAll(Pane pane, const QString &replacement);
    void resync(Pane pane);
    void apply(Pane pane);
    void updateStatus(Pane pane);

    MarkdownEditor *m_editor = nullptr;
    MarkdownView *m_viewer = nullptr;
    FindBar *m_editorBar = nullptr;
    FindBar *m_viewerBar = nullptr;
    FindState m_editorState;
    FindState m_viewerState;
    bool m_editorStale = false;
    bool m_viewerStale = false;
};

#endif // TOTALMARKDOWN_APP_FINDCONTROLLER_H
