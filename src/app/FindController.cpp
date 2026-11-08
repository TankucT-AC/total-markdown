// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/FindController.h"

#include "editor/MarkdownEditor.h"
#include "viewer/FindBar.h"
#include "viewer/MarkdownView.h"

#include <QApplication>
#include <QTextCursor>
#include <QTextDocument>

FindController::FindController(MarkdownEditor *editor, FindBar *editorBar, MarkdownView *viewer, FindBar *viewerBar, QObject *parent)
    : QObject(parent)
    , m_editor(editor)
    , m_viewer(viewer)
    , m_editorBar(editorBar)
    , m_viewerBar(viewerBar)
{
    connect(m_editorBar, &FindBar::queryChanged, this, [this](const QString &query, bool caseSensitive) {
        run(Pane::Editor, query, caseSensitive);
    });
    connect(m_editorBar, &FindBar::findNextRequested, this, [this] {
        step(Pane::Editor, 1);
    });
    connect(m_editorBar, &FindBar::findPreviousRequested, this, [this] {
        step(Pane::Editor, -1);
    });
    connect(m_editorBar, &FindBar::closed, this, [this] {
        close(Pane::Editor);
    });
    connect(m_editorBar, &FindBar::replaceRequested, this, [this](const QString &rep) {
        replaceCurrent(Pane::Editor, rep);
    });
    connect(m_editorBar, &FindBar::replaceAllRequested, this, [this](const QString &rep) {
        replaceAll(Pane::Editor, rep);
    });

    m_viewerBar->setReplaceEnabled(false);
    connect(m_viewerBar, &FindBar::queryChanged, this, [this](const QString &query, bool caseSensitive) {
        run(Pane::Viewer, query, caseSensitive);
    });
    connect(m_viewerBar, &FindBar::findNextRequested, this, [this] {
        step(Pane::Viewer, 1);
    });
    connect(m_viewerBar, &FindBar::findPreviousRequested, this, [this] {
        step(Pane::Viewer, -1);
    });
    connect(m_viewerBar, &FindBar::closed, this, [this] {
        close(Pane::Viewer);
    });

    // An edit moves every offset, so the decorations are dropped and the next
    // step recomputes them. Searching a huge file on each keystroke would undo
    // the whole point of the size limits.
    connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this] {
        if (m_editorState.isEmpty())
            return;
        m_editor->clearFindMatches();
        m_editorStale = true;
    });

    // A re-render rebuilds the document from scratch: the hits have to be found
    // again, but the user's place in them should not move.
    connect(m_viewer, &MarkdownView::rendered, this, [this] {
        if (m_viewerBar->isVisible())
            resync(Pane::Viewer);
    });
}

FindController::Pane FindController::otherPane(Pane pane)
{
    return pane == Pane::Editor ? Pane::Viewer : Pane::Editor;
}

FindState &FindController::stateOf(Pane pane)
{
    return pane == Pane::Editor ? m_editorState : m_viewerState;
}

const FindState &FindController::stateOf(Pane pane) const
{
    return pane == Pane::Editor ? m_editorState : m_viewerState;
}

FindBar *FindController::barOf(Pane pane) const
{
    return pane == Pane::Editor ? m_editorBar : m_viewerBar;
}

QString FindController::textOf(Pane pane) const
{
    if (pane == Pane::Editor)
        return m_editor->toPlainText();
    return m_viewer->document()->toPlainText();
}

int FindController::anchorPositionOf(Pane pane) const
{
    if (pane == Pane::Editor)
        return m_editor->textCursor().position();
    // Search from what the reader is looking at, not from the top of the file.
    return m_viewer->cursorForPosition(QPoint(0, 0)).position();
}

bool FindController::stale(Pane pane) const
{
    return pane == Pane::Editor ? m_editorStale : m_viewerStale;
}

void FindController::setStale(Pane pane, bool value)
{
    (pane == Pane::Editor ? m_editorStale : m_viewerStale) = value;
}

bool FindController::upToDate(Pane pane) const
{
    return !stale(pane) && stateOf(pane).query() == barOf(pane)->query() && stateOf(pane).caseSensitive() == barOf(pane)->caseSensitive();
}

FindController::Pane FindController::focusedPane() const
{
    QWidget *focus = QApplication::focusWidget();
    if (!focus)
        return Pane::Editor;
    // The find strips are siblings of the panes, so they count as part of the
    // pane they belong to: F3 while typing in the viewer's strip must walk the
    // viewer's matches.
    const bool inViewer = focus == m_viewer || m_viewer->isAncestorOf(focus) || focus == m_viewerBar || m_viewerBar->isAncestorOf(focus);
    return inViewer ? Pane::Viewer : Pane::Editor;
}

void FindController::openForFocusedPane()
{
    show(focusedPane());
}

void FindController::openReplaceForFocusedPane()
{
    show(Pane::Editor);
    m_editorBar->setReplaceVisible(true);
    if (!m_editorBar->query().isEmpty())
        m_editorBar->focusReplaceInput();
    else
        m_editorBar->focusInput();
}

void FindController::findNext()
{
    const Pane pane = focusedPane();
    if (barOf(pane)->isVisible()) {
        step(pane, 1);
        return;
    }
    show(pane); // opens the bar and searches from the caret
}

void FindController::findPrevious()
{
    const Pane pane = focusedPane();
    if (barOf(pane)->isVisible()) {
        step(pane, -1);
        return;
    }
    show(pane);
}

void FindController::reset()
{
    close(Pane::Editor);
    close(Pane::Viewer);
    m_editorBar->clearQuery();
    m_viewerBar->clearQuery();
    m_editorState.clear();
    m_viewerState.clear();
    m_editorStale = false;
    m_viewerStale = false;
}

void FindController::show(Pane pane)
{
    // One strip at a time: two open bars would report different counts for the
    // same document and neither would be wrong.
    close(otherPane(pane));

    FindBar *bar = barOf(pane);
    bar->show();
    bar->focusInput();

    if (bar->query().isEmpty())
        bar->clearStatus();
    else
        run(pane, bar->query(), bar->caseSensitive());
}

void FindController::close(Pane pane)
{
    FindBar *bar = barOf(pane);
    if (!bar->isVisible() && stateOf(pane).isEmpty())
        return;

    bar->hide();
    stateOf(pane).clear();
    setStale(pane, false);

    QWidget *target = nullptr;
    if (pane == Pane::Editor) {
        m_editor->clearFindMatches();
        target = m_editor;
    } else {
        m_viewer->clearFindMatches();
        target = m_viewer;
    }
    // Hand the keyboard back to the pane the user was reading.
    target->setFocus(Qt::OtherFocusReason);
}

void FindController::run(Pane pane, const QString &query, bool caseSensitive)
{
    FindBar *bar = barOf(pane);
    FindState &state = stateOf(pane);

    state.setQuery(query, caseSensitive);
    setStale(pane, false);

    // The preview of a huge file is not rendered at all, so there is nothing to
    // search: say that, rather than "no results".
    if (pane == Pane::Viewer && m_viewer->previewPaused()) {
        state.clear();
        m_viewer->clearFindMatches();
        bar->setStatus(tr("preview paused"));
        return;
    }

    state.refresh(textOf(pane));

    if (state.isEmpty()) {
        bar->clearStatus(); // an empty query is not a failed search
        if (pane == Pane::Editor)
            m_editor->clearFindMatches();
        else
            m_viewer->clearFindMatches();
        return;
    }

    // A fresh search starts where the user is, not at the top of the document.
    state.selectNext(anchorPositionOf(pane));
    apply(pane);
    updateStatus(pane);
}

void FindController::step(Pane pane, int direction)
{
    if (!upToDate(pane)) {
        run(pane, barOf(pane)->query(), barOf(pane)->caseSensitive());
        return;
    }

    FindState &state = stateOf(pane);
    if (state.matchCount() == 0)
        return;

    // "Next" continues past the current hit rather than landing on it again.
    const FindState::Match current = state.current();
    if (direction > 0)
        state.selectNext(current.position + current.length);
    else
        state.selectPrevious(current.position);

    apply(pane);
    updateStatus(pane);
}

void FindController::replaceCurrent(Pane pane, const QString &replacement)
{
    if (pane != Pane::Editor)
        return;

    if (!upToDate(pane)) {
        run(pane, barOf(pane)->query(), barOf(pane)->caseSensitive());
    }

    FindState &state = stateOf(pane);
    if (state.matchCount() == 0 || state.currentIndex() < 0)
        return;

    const FindState::Match match = state.current();
    QTextCursor cursor(m_editor->document());
    cursor.setPosition(match.position);
    cursor.setPosition(match.position + match.length, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);

    run(pane, barOf(pane)->query(), barOf(pane)->caseSensitive());
}

void FindController::replaceAll(Pane pane, const QString &replacement)
{
    if (pane != Pane::Editor)
        return;

    if (!upToDate(pane)) {
        run(pane, barOf(pane)->query(), barOf(pane)->caseSensitive());
    }

    FindState &state = stateOf(pane);
    if (state.matchCount() == 0)
        return;

    const QVector<FindState::Match> matches = state.matches();
    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();
    for (int i = int(matches.size()) - 1; i >= 0; --i) {
        const FindState::Match &m = matches.at(i);
        cursor.setPosition(m.position);
        cursor.setPosition(m.position + m.length, QTextCursor::KeepAnchor);
        cursor.insertText(replacement);
    }
    cursor.endEditBlock();

    run(pane, barOf(pane)->query(), barOf(pane)->caseSensitive());
}

void FindController::resync(Pane pane)
{
    FindState &state = stateOf(pane);
    if (state.isEmpty())
        return;

    state.refresh(textOf(pane)); // keeps the current match by index
    apply(pane);
    updateStatus(pane);
}

void FindController::apply(Pane pane)
{
    const FindState &state = stateOf(pane);
    if (pane == Pane::Editor)
        m_editor->setFindMatches(state.matches(), state.currentIndex());
    else
        m_viewer->setFindMatches(state.matches(), state.currentIndex());
}

void FindController::updateStatus(Pane pane)
{
    const FindState &state = stateOf(pane);
    FindBar *bar = barOf(pane);

    if (state.isEmpty())
        bar->clearStatus();
    else if (state.matchCount() == 0)
        bar->setStatus(tr("no results"));
    else
        bar->setStatus(tr("%1 of %2").arg(state.currentNumber()).arg(state.matchCount()));
}
