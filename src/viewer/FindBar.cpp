// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewer/FindBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
    , m_input(new QLineEdit(this))
    , m_status(new QLabel(this))
    , m_toggleReplace(new QToolButton(this))
    , m_previous(new QToolButton(this))
    , m_next(new QToolButton(this))
    , m_case(new QToolButton(this))
    , m_replaceRow(new QWidget(this))
    , m_replaceInput(new QLineEdit(m_replaceRow))
    , m_replaceNext(new QToolButton(m_replaceRow))
    , m_replaceAll(new QToolButton(m_replaceRow))
{
    m_input->setPlaceholderText(tr("Find"));
    m_input->setClearButtonEnabled(true);
    m_input->installEventFilter(this); // to see Escape and Shift+Enter

    // A fixed slot for the counter, so the row does not jump as it changes.
    m_status->setMinimumWidth(72);

    m_toggleReplace->setText(QStringLiteral("▼"));
    m_toggleReplace->setCheckable(true);
    m_toggleReplace->setToolTip(tr("Toggle Replace (Ctrl+H)"));

    m_previous->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_previous->setToolTip(tr("Previous match (Shift+F3)"));
    m_next->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_next->setToolTip(tr("Next match (F3)"));

    m_case->setText(tr("Aa"));
    m_case->setCheckable(true);
    m_case->setToolTip(tr("Match case"));

    auto *close = new QToolButton(this);
    close->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    close->setToolTip(tr("Close (Esc)"));

    auto *findLayout = new QHBoxLayout;
    findLayout->setContentsMargins(0, 0, 0, 0);
    findLayout->setSpacing(4);
    findLayout->addWidget(m_toggleReplace);
    findLayout->addWidget(m_input, 1);
    findLayout->addWidget(m_status);
    findLayout->addWidget(m_case);
    findLayout->addWidget(m_previous);
    findLayout->addWidget(m_next);
    findLayout->addWidget(close);

    m_replaceInput->setPlaceholderText(tr("Replace"));
    m_replaceInput->setClearButtonEnabled(true);
    m_replaceInput->installEventFilter(this);

    m_replaceNext->setText(tr("Replace"));
    m_replaceNext->setToolTip(tr("Replace current match"));

    m_replaceAll->setText(tr("Replace All"));
    m_replaceAll->setToolTip(tr("Replace all matches in document"));

    auto *replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(4);
    replaceLayout->addSpacing(28); // align with find input after toggle button
    replaceLayout->addWidget(m_replaceInput, 1);
    replaceLayout->addWidget(m_replaceNext);
    replaceLayout->addWidget(m_replaceAll);
    m_replaceRow->hide();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 4, 6, 4);
    mainLayout->setSpacing(4);
    mainLayout->addLayout(findLayout);
    mainLayout->addWidget(m_replaceRow);

    connect(m_input, &QLineEdit::textChanged, this, &FindBar::emitQueryChanged);
    connect(m_case, &QToolButton::toggled, this, &FindBar::emitQueryChanged);
    connect(m_toggleReplace, &QToolButton::toggled, this, &FindBar::setReplaceVisible);
    connect(m_next, &QToolButton::clicked, this, &FindBar::findNextRequested);
    connect(m_previous, &QToolButton::clicked, this, &FindBar::findPreviousRequested);
    connect(m_replaceNext, &QToolButton::clicked, this, [this] {
        emit replaceRequested(m_replaceInput->text());
    });
    connect(m_replaceAll, &QToolButton::clicked, this, [this] {
        emit replaceAllRequested(m_replaceInput->text());
    });
    connect(close, &QToolButton::clicked, this, &FindBar::closed);
}

void FindBar::emitQueryChanged()
{
    emit queryChanged(m_input->text(), m_case->isChecked());
}

QString FindBar::query() const
{
    return m_input->text();
}

bool FindBar::caseSensitive() const
{
    return m_case->isChecked();
}

void FindBar::clearQuery()
{
    m_input->clear(); // the change signal does the rest
}

void FindBar::setQuery(const QString &query)
{
    m_input->setText(query); // the change signal runs the search
}

void FindBar::setStatus(const QString &status)
{
    m_status->setText(status);
}

QString FindBar::status() const
{
    return m_status->text();
}

void FindBar::clearStatus()
{
    m_status->clear();
}

void FindBar::focusInput()
{
    m_input->setFocus(Qt::ShortcutFocusReason);
    m_input->selectAll();
}

void FindBar::setReplaceEnabled(bool enabled)
{
    m_replaceEnabled = enabled;
    m_toggleReplace->setVisible(enabled);
    if (!enabled && isReplaceVisible())
        setReplaceVisible(false);
}

bool FindBar::replaceEnabled() const
{
    return m_replaceEnabled;
}

void FindBar::setReplaceVisible(bool visible)
{
    if (!m_replaceEnabled && visible)
        return;

    m_toggleReplace->setChecked(visible);
    m_toggleReplace->setText(visible ? QStringLiteral("▲") : QStringLiteral("▼"));
    m_replaceRow->setVisible(visible);
}

bool FindBar::isReplaceVisible() const
{
    return m_replaceRow->isVisible();
}

void FindBar::focusReplaceInput()
{
    setReplaceVisible(true);
    m_replaceInput->setFocus(Qt::ShortcutFocusReason);
    m_replaceInput->selectAll();
}

QString FindBar::replacementText() const
{
    return m_replaceInput->text();
}

void FindBar::setReplacementText(const QString &text)
{
    m_replaceInput->setText(text);
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_input || watched == m_replaceInput) && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            emit closed();
            return true;
        }
        if (watched == m_replaceInput && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            emit replaceRequested(m_replaceInput->text());
            return true;
        }
        if (watched == m_input) {
            switch (key->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_F3:
                if (key->modifiers() & Qt::ShiftModifier)
                    emit findPreviousRequested();
                else
                    emit findNextRequested();
                return true;
            default:
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    // Reached when a button has the focus: Escape still closes the bar, and the
    // buttons stay reachable by keyboard.
    if (event->key() == Qt::Key_Escape) {
        emit closed();
        return;
    }
    QWidget::keyPressEvent(event);
}
