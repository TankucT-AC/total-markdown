// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOTALMARKDOWN_APP_MAINWINDOW_H
#define TOTALMARKDOWN_APP_MAINWINDOW_H

#include "core/DocumentLimits.h"
#include "core/Outline.h"
#include "core/ThemeManager.h"

#include <QHash>
#include <QMainWindow>
#include <QVector>

class QAction;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QSplitter;
class FileDocument;
class FileTreePanel;
class FindBar;
class FindController;
class MarkdownEditor;
class MarkdownView;
class OutlinePanel;
class SidebarTitleBar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    QWidget *editorPanel() const;
    QWidget *viewerPanel() const;
    QAction *toggleEditorAction() const;
    QAction *toggleViewerAction() const;
    ThemeManager *themeManager() const;
    MarkdownEditor *editor() const;
    MarkdownView *viewer() const;
    FileDocument *fileDocument() const;

    // Opens a document by path; returns false and reports the error otherwise.
    bool openFile(const QString &path);

    // Shows or hides the sidebar dock, with its Outline tab current when
    // shown. Readers toggle the dock with Ctrl+B; the screenshot harness
    // calls this to frame the panel deliberately.
    void setSidebarVisible(bool visible);

    // Opens the find strip over the source pane with query pre-typed, as if
    // the reader had focused the editor and pressed Ctrl+F and typed it.
    void openFind(const QString &query);

    // Typesets the preview and the editor with the chosen families and keeps
    // the choice in QSettings. Blank families mean the bundled faces, so a
    // blank pair resets to them; the Fonts dialog funnels here.
    void applyFontChoice(const QString &bodyFamily, const QString &codeFamily);

    // Saves the current document (prompting for a path when untitled).
    bool saveFile();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void buildSidebar();
    void applyTheme(const ThemeDefinition &theme);
    void loadThemeFromFile();
    void restoreThemeSettings();
    void persistThemeSettings();
    void restorePaneSettings();
    void persistPaneSettings();
    void restoreRemoteImageSettings();
    void restoreFontSettings();
    void chooseFonts();
    void releaseOutlinePin();

    void updateStatusBar();
    void updateRenderStatus();
    void updateCursorStatus();
    void updateDocStats();
    void toggleWordWrap(bool wrap);
    void restoreWordWrapSettings();
    void updateRecentFilesMenu();
    void addRecentFile(const QString &path);
    void clearRecentFiles();
    void zoomIn();
    void zoomOut();
    void resetZoom();

    void openFolderDialog();
    void setFolder(const QString &path);
    void refreshOutline();
    void activateOutlineEntry(int index);

    void newFile();
    void openFileDialog();
    bool saveFileAs();
    bool maybeSave();
    void updateWindowTitle();
    void applyFileContent(const QString &content, const QString &path);
    void onExternalChange();

    QSplitter *m_splitter = nullptr;
    QWidget *m_editorPane = nullptr;
    QWidget *m_viewerPane = nullptr;
    MarkdownEditor *m_editor = nullptr;
    MarkdownView *m_viewer = nullptr;
    FindBar *m_editorBar = nullptr;
    FindBar *m_viewerBar = nullptr;
    FindController *m_find = nullptr;
    QDockWidget *m_sidebar = nullptr;
    OutlinePanel *m_outlinePanel = nullptr;
    SidebarTitleBar *m_sidebarTitleBar = nullptr;
    FileTreePanel *m_filesPanel = nullptr;
    QVector<OutlineEntry> m_outline;
    // The outline row the reader clicked, as an index into m_outline; -1 when
    // the bar follows the reading position. Clicking pins the bar until the
    // reader scrolls the preview themselves.
    int m_outlinePin = -1;
    FileDocument *m_file = nullptr;
    ThemeManager *m_themes = nullptr;
    DocumentLimits m_limits;
    bool m_loading = false;
    QMenu *m_recentFilesMenu = nullptr;
    QLabel *m_statusInfo = nullptr;
    QLabel *m_statusCursor = nullptr;
    QLabel *m_statusStats = nullptr;
    QLabel *m_statusWrap = nullptr;
    QLabel *m_statusRender = nullptr;
    QAction *m_toggleEditor = nullptr;
    QAction *m_toggleViewer = nullptr;
    QAction *m_toggleWordWrap = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QAction *m_toggleRemoteImages = nullptr;
    QAction *m_customThemeAction = nullptr;
    QHash<int, QAction *> m_themeModeActions;
};

#endif // TOTALMARKDOWN_APP_MAINWINDOW_H
