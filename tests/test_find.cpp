// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "app/FindController.h"
#include "core/DocumentLimits.h"
#include "core/FindState.h"
#include "editor/MarkdownEditor.h"
#include "viewer/FindBar.h"
#include "viewer/MarkdownView.h"

#include <QTextEdit>

class FindTest : public QObject
{
    Q_OBJECT

private slots:
    void findsEveryOccurrence();
    void matchesDoNotOverlap();
    void searchIsLiteral();
    void caseSensitivityIsHonoured();
    void emptyQueryMatchesNothing();
    void nextWrapsToTheFirstMatch();
    void previousWrapsToTheLastMatch();
    void refreshKeepsTheCurrentMatch();
    void refreshClearsWhenTheTextLosesEveryMatch();
    void editorHighlightsEveryMatch();
    void clearingRemovesTheDecorations();
    void previewHighlightsEveryMatch();
    void findBarDrivesTheController();
    void pausedPreviewReportsItself();
    void replaceCurrentReplacesSingleMatch();
    void replaceAllReplacesAllMatches();
};

void FindTest::findsEveryOccurrence()
{
    FindState state;
    state.setQuery(QStringLiteral("one"), false);
    state.refresh(QStringLiteral("one two one two one"));

    QCOMPARE(state.matchCount(), 3);
    QCOMPARE(state.matches().at(0).position, 0);
    QCOMPARE(state.matches().at(1).position, 8);
    QCOMPARE(state.matches().at(2).position, 16);
    QCOMPARE(state.matches().at(0).length, 3);
}

void FindTest::matchesDoNotOverlap()
{
    FindState state;
    state.setQuery(QStringLiteral("aa"), false);
    state.refresh(QStringLiteral("aaaa"));

    QCOMPARE(state.matchCount(), 2);
    QCOMPARE(state.matches().at(1).position, 2);
}

void FindTest::searchIsLiteral()
{
    FindState state;
    state.setQuery(QStringLiteral("a.b"), false);
    state.refresh(QStringLiteral("a.b axb a.b"));

    // The dot is a dot, not "any character".
    QCOMPARE(state.matchCount(), 2);
}

void FindTest::caseSensitivityIsHonoured()
{
    FindState state;
    state.setQuery(QStringLiteral("foo"), false);
    state.refresh(QStringLiteral("Foo foo"));
    QCOMPARE(state.matchCount(), 2);

    state.setQuery(QStringLiteral("foo"), true);
    state.refresh(QStringLiteral("Foo foo"));
    QCOMPARE(state.matchCount(), 1);
    QCOMPARE(state.matches().at(0).position, 4);
}

void FindTest::emptyQueryMatchesNothing()
{
    FindState state;
    state.setQuery(QString(), false);
    state.refresh(QStringLiteral("anything at all"));

    QVERIFY(state.isEmpty());
    QCOMPARE(state.matchCount(), 0);
    QCOMPARE(state.currentIndex(), -1);
    QCOMPARE(state.currentNumber(), 0);
}

void FindTest::nextWrapsToTheFirstMatch()
{
    FindState state;
    state.setQuery(QStringLiteral("one"), false);
    state.refresh(QStringLiteral("one two one two one"));

    state.selectNext(0); // the caret is on the first match
    QCOMPARE(state.currentNumber(), 1);
    state.selectNext(1); // past its start
    QCOMPARE(state.currentNumber(), 2);
    state.selectNext(17); // nothing left: wrap
    QCOMPARE(state.currentNumber(), 1);
}

void FindTest::previousWrapsToTheLastMatch()
{
    FindState state;
    state.setQuery(QStringLiteral("one"), false);
    state.refresh(QStringLiteral("one two one two one"));

    state.selectPrevious(16); // the last match's own start
    QCOMPARE(state.currentNumber(), 2);
    state.selectPrevious(0); // nothing before the top: wrap
    QCOMPARE(state.currentNumber(), 3);
}

void FindTest::refreshKeepsTheCurrentMatch()
{
    FindState state;
    state.setQuery(QStringLiteral("one"), false);
    state.refresh(QStringLiteral("one two one"));
    state.selectNext(2);
    QCOMPARE(state.currentNumber(), 2);

    state.refresh(QStringLiteral("one two one"));
    QCOMPARE(state.currentNumber(), 2);
}

void FindTest::refreshClearsWhenTheTextLosesEveryMatch()
{
    FindState state;
    state.setQuery(QStringLiteral("one"), false);
    state.refresh(QStringLiteral("one two one"));
    QCOMPARE(state.matchCount(), 2);

    state.refresh(QStringLiteral("two three"));
    QCOMPARE(state.matchCount(), 0);
    QCOMPARE(state.currentIndex(), -1);
}

void FindTest::editorHighlightsEveryMatch()
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("one two one two one"));

    const QVector<FindState::Match> matches = {{0, 3}, {8, 3}, {16, 3}};
    editor.setFindMatches(matches, 1);

    // The current-line tint plus one selection per hit.
    const QList<QTextEdit::ExtraSelection> selections = editor.extraSelections();
    QCOMPARE(selections.size(), 4);

    const QColor highlight = editor.palette().color(QPalette::Highlight);
    int faint = 0;
    int strong = 0;
    for (const QTextEdit::ExtraSelection &selection : selections) {
        const QColor color = selection.format.background().color();
        if (color.rgb() != highlight.rgb())
            continue;
        if (color.alpha() == 255)
            ++strong;
        else
            ++faint;
    }
    QCOMPARE(faint, 2); // every other hit
    QCOMPARE(strong, 1); // the current one

    // And the caret rides the current hit, so the user can type from there.
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("one"));
}

void FindTest::clearingRemovesTheDecorations()
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("one two one"));
    editor.setFindMatches({{0, 3}, {8, 3}}, 0);
    QCOMPARE(editor.extraSelections().size(), 3);

    editor.clearFindMatches();
    QCOMPARE(editor.extraSelections().size(), 1); // the current line is always there
}

void FindTest::previewHighlightsEveryMatch()
{
    MarkdownView view;
    view.setRenderInterval(0);
    view.setMarkdown(QStringLiteral("# one\n\ntwo one three\n"));

    const QString text = view.document()->toPlainText();
    const qsizetype first = text.indexOf(QStringLiteral("one"));
    const qsizetype second = text.indexOf(QStringLiteral("one"), first + 3);
    QVERIFY(first >= 0 && second > first);

    view.setFindMatches({{int(first), 3}, {int(second), 3}}, 0);
    QCOMPARE(view.extraSelections().size(), 2);

    view.clearFindMatches();
    QVERIFY(view.extraSelections().isEmpty());
}

void FindTest::findBarDrivesTheController()
{
    MarkdownEditor editor;
    FindBar editorBar;
    MarkdownView viewer;
    FindBar viewerBar;
    viewer.setRenderInterval(0);
    FindController controller(&editor, &editorBar, &viewer, &viewerBar);

    editor.setMarkdown(QStringLiteral("alpha beta alpha gamma alpha"));
    editorBar.show();
    editorBar.setQuery(QStringLiteral("alpha"));

    QCOMPARE(editorBar.status(), QStringLiteral("1 of 3"));
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("alpha"));

    editorBar.findNextRequested();
    QCOMPARE(editorBar.status(), QStringLiteral("2 of 3"));
    editorBar.findNextRequested();
    QCOMPARE(editorBar.status(), QStringLiteral("3 of 3"));
    editorBar.findNextRequested(); // wraps
    QCOMPARE(editorBar.status(), QStringLiteral("1 of 3"));

    editorBar.findPreviousRequested(); // wraps backwards
    QCOMPARE(editorBar.status(), QStringLiteral("3 of 3"));

    // A query nothing matches says so, rather than reporting "0 of 0".
    editorBar.setQuery(QStringLiteral("nothing here"));
    QCOMPARE(editorBar.status(), QStringLiteral("no results"));
    QCOMPARE(editor.extraSelections().size(), 1);
}

void FindTest::pausedPreviewReportsItself()
{
    MarkdownEditor editor;
    FindBar editorBar;
    MarkdownView viewer;
    FindBar viewerBar;
    FindController controller(&editor, &editorBar, &viewer, &viewerBar);

    viewer.setDocumentLimits(DocumentLimits::forSize(8 * 1024 * 1024));
    QVERIFY(viewer.previewPaused());

    viewerBar.show();
    viewerBar.setQuery(QStringLiteral("anything"));
    // "no results" would be a lie: the document was never rendered.
    QCOMPARE(viewerBar.status(), QStringLiteral("preview paused"));
    QVERIFY(viewer.extraSelections().isEmpty());
}

void FindTest::replaceCurrentReplacesSingleMatch()
{
    MarkdownEditor editor;
    FindBar editorBar;
    MarkdownView viewer;
    FindBar viewerBar;
    FindController controller(&editor, &editorBar, &viewer, &viewerBar);

    editor.setMarkdown(QStringLiteral("one two one three"));
    editorBar.show();
    editorBar.setQuery(QStringLiteral("one"));
    QCOMPARE(editorBar.status(), QStringLiteral("1 of 2"));

    emit editorBar.replaceRequested(QStringLiteral("TEN"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("TEN two one three"));
    QCOMPARE(editorBar.status(), QStringLiteral("1 of 1"));
}

void FindTest::replaceAllReplacesAllMatches()
{
    MarkdownEditor editor;
    FindBar editorBar;
    MarkdownView viewer;
    FindBar viewerBar;
    FindController controller(&editor, &editorBar, &viewer, &viewerBar);

    editor.setMarkdown(QStringLiteral("one two one three one"));
    editorBar.show();
    editorBar.setQuery(QStringLiteral("one"));
    QCOMPARE(editorBar.status(), QStringLiteral("1 of 3"));

    emit editorBar.replaceAllRequested(QStringLiteral("ALL"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("ALL two ALL three ALL"));
    QCOMPARE(editorBar.status(), QStringLiteral("no results"));
}

QTEST_MAIN(FindTest)
#include "test_find.moc"
