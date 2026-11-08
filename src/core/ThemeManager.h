// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_THEMEMANAGER_H
#define TOTALMARKDOWN_CORE_THEMEMANAGER_H

#include "core/Theme.h"

#include <QObject>

/*
 * Tracks the active theme mode (Light / Dark / System / Custom) and resolves
 * it into the concrete ThemeDefinition the rest of the app renders with.
 *
 * In System mode the manager follows QStyleHints::colorScheme() live: when the
 * platform announces a scheme change, themeChanged() is emitted with the new
 * resolved definition. Consumers repaint/re-render in response; nothing needs
 * to poll.
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Light,
        Dark,
        System,
        Custom,
    };
    Q_ENUM(Mode)

    explicit ThemeManager(QObject *parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const;

    void setCustomTheme(const ThemeDefinition &theme);

    ThemeDefinition currentTheme() const;

signals:
    void themeChanged(const ThemeDefinition &theme);
    void modeChanged(Mode mode);

private:
    void maybeEmitChange();
    ThemeDefinition resolve() const;

    Mode m_mode = Mode::System;
    ThemeDefinition m_custom = ThemeDefinition::light();
};

#endif // TOTALMARKDOWN_CORE_THEMEMANAGER_H
