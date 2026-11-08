// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_FINDSTATE_H
#define TOTALMARKDOWN_CORE_FINDSTATE_H

#include <QString>
#include <QVector>

/*
 * The searching half of a find bar: a query, the matches it produces in a text,
 * and which match is current. It owns no widgets and no document, so both panes
 * can share it and tests can drive it directly.
 *
 * Matches are non-overlapping and left to right, and the search is literal: the
 * query is never a pattern, which is what a find bar promises.
 */
class FindState
{
public:
    struct Match {
        int position = -1; // offset in the searched text, in QString characters
        int length = 0;
    };

    void setQuery(const QString &query, bool caseSensitive);

    // Recomputes the matches against a fresh copy of the text. The current
    // selection is kept by index while it still exists, so a re-run after an
    // edit does not jump the user somewhere else.
    void refresh(const QString &text);

    bool isEmpty() const
    {
        return m_query.isEmpty();
    }
    QString query() const
    {
        return m_query;
    }
    bool caseSensitive() const
    {
        return m_caseSensitive;
    }

    const QVector<Match> &matches() const
    {
        return m_matches;
    }
    int matchCount() const
    {
        return int(m_matches.size());
    }
    int currentIndex() const
    {
        return m_current;
    }
    int currentNumber() const
    {
        return m_current < 0 ? 0 : m_current + 1;
    }
    Match current() const;

    // Moves the current match. "Next" continues from `fromPosition`, so passing
    // the caret keeps the search anchored where the user is. Both directions
    // wrap around the document.
    void selectNext(int fromPosition);
    void selectPrevious(int before);

    void clear();

    // Index of the first match starting at or after `position`, or matchCount()
    // when there is none. The mirror image returns -1.
    static int firstIndexAtOrAfter(const QVector<Match> &matches, int position);
    static int lastIndexBefore(const QVector<Match> &matches, int position);

private:
    QVector<Match> findMatches(const QString &text) const;

    QString m_query;
    bool m_caseSensitive = false;
    QVector<Match> m_matches;
    int m_current = -1;
};

#endif // TOTALMARKDOWN_CORE_FINDSTATE_H
