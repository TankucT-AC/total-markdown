// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_CORE_SYSTEMTHEMEPROVIDER_H
#define TOTALMARKDOWN_CORE_SYSTEMTHEMEPROVIDER_H

#include "core/Theme.h"

#include <QString>
#include <QVector>

#include <optional>

/*
 * Reads KDE colour schemes (the *.colors files Dolphin and other Plasma apps
 * use) and turns them into ThemeDefinition values.
 *
 * This is deliberately dependency-free: a .colors file is plain INI, parsed
 * with QSettings, so no KDE Frameworks / KConfig is pulled in and the app stays
 * portable. When no such files exist (non-KDE desktops, other platforms) every
 * method simply reports nothing and the app falls back to its built-in themes.
 */
class SystemThemeProvider
{
public:
    struct Scheme {
        QString name;
        QString path;
    };

    // Colour schemes found in the standard locations
    // (~/.local/share/color-schemes and the XDG data dirs), newest files first
    // within each directory.
    static QVector<Scheme> availableSchemes();

    // Parses one *.colors file. Returns std::nullopt if it is missing the
    // essential colour groups.
    static std::optional<ThemeDefinition> loadScheme(const QString &path);

    // The scheme the desktop is currently using, resolved from kdeglobals'
    // [General] ColorScheme entry. Empty when it cannot be determined.
    static QString currentSchemePath();

    // Loads the current desktop scheme, if any.
    static std::optional<ThemeDefinition> currentSystemTheme();
};

#endif // TOTALMARKDOWN_CORE_SYSTEMTHEMEPROVIDER_H
