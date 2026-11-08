// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/MainWindow.h"

#include "app/FileTreePanel.h"
#include "app/FindController.h"
#include "app/FontsDialog.h"
#include "app/OutlinePanel.h"
#include "app/SidebarTitleBar.h"
#include "core/FileDocument.h"
#include "core/SystemThemeProvider.h"
#include "core/ThemeManager.h"
#include "editor/MarkdownEditor.h"
#include "viewer/FindBar.h"
#include "viewer/MarkdownView.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace
{

QString modeName(ThemeManager::Mode mode)
{
    switch (mode) {
    case ThemeManager::Mode::Light:
        return QStringLiteral("light");
    case ThemeManager::Mode::Dark:
        return QStringLiteral("dark");
    case ThemeManager::Mode::Custom:
        return QStringLiteral("custom");
    case ThemeManager::Mode::System:
        break;
    }
    return QStringLiteral("system");
}

ThemeManager::Mode modeFromName(const QString &name)
{
    if (name == QLatin1String("light"))
        return ThemeManager::Mode::Light;
    if (name == QLatin1String("dark"))
        return ThemeManager::Mode::Dark;
    if (name == QLatin1String("custom"))
        return ThemeManager::Mode::Custom;
    return ThemeManager::Mode::System;
}

} // namespace

#include <array>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_file(new FileDocument(this))
    , m_themes(new ThemeManager(this))
{
    buildUi();
    buildMenus();
    connect(m_themes, &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
    connect(m_themes, &ThemeManager::modeChanged, this, [this](ThemeManager::Mode mode) {
        if (QAction *action = m_themeModeActions.value(int(mode)))
            action->setChecked(true);
        persistThemeSettings();
    });
    connect(m_file, &FileDocument::externallyChanged, this, &MainWindow::onExternalChange);
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateWindowTitle);

    applyTheme(m_themes->currentTheme());
    restoreThemeSettings();
    restoreFontSettings();
    restoreWordWrapSettings();
    setFolder(QSettings().value(QStringLiteral("files/folder")).toString());
    updateWindowTitle();
    updateCursorStatus();
    updateDocStats();
    resize(1200, 800);
}

QWidget *MainWindow::editorPanel() const
{
    return m_editorPane;
}

QWidget *MainWindow::viewerPanel() const
{
    return m_viewerPane;
}

QAction *MainWindow::toggleEditorAction() const
{
    return m_toggleEditor;
}

QAction *MainWindow::toggleViewerAction() const
{
    return m_toggleViewer;
}

ThemeManager *MainWindow::themeManager() const
{
    return m_themes;
}

MarkdownEditor *MainWindow::editor() const
{
    return m_editor;
}

MarkdownView *MainWindow::viewer() const
{
    return m_viewer;
}

FileDocument *MainWindow::fileDocument() const
{
    return m_file;
}

void MainWindow::buildUi()
{
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Each pane is a container: the find strip lives directly above its view and
    // hides with it.
    m_editorPane = new QWidget(m_splitter);
    m_editorBar = new FindBar(m_editorPane);
    m_editor = new MarkdownEditor(m_editorPane);
    auto *editorLayout = new QVBoxLayout(m_editorPane);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);
    editorLayout->addWidget(m_editorBar);
    editorLayout->addWidget(m_editor, 1);

    m_viewerPane = new QWidget(m_splitter);
    m_viewerBar = new FindBar(m_viewerPane);
    m_viewer = new MarkdownView(m_viewerPane);
    auto *viewerLayout = new QVBoxLayout(m_viewerPane);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(0);
    viewerLayout->addWidget(m_viewerBar);
    viewerLayout->addWidget(m_viewer, 1);

    m_splitter->addWidget(m_editorPane);
    m_splitter->addWidget(m_viewerPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);

    setCentralWidget(m_splitter);

    m_editorBar->hide();
    m_viewerBar->hide();
    m_find = new FindController(m_editor, m_editorBar, m_viewer, m_viewerBar, this);

    m_statusInfo = new QLabel(this);
    m_statusCursor = new QLabel(this);
    m_statusStats = new QLabel(this);
    m_statusWrap = new QLabel(this);
    m_statusRender = new QLabel(this);

    statusBar()->addWidget(m_statusInfo);
    statusBar()->addWidget(m_statusCursor);
    statusBar()->addWidget(m_statusStats);
    statusBar()->addWidget(m_statusWrap);
    statusBar()->addPermanentWidget(m_statusRender);

    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading)
            return;
        const QString text = m_editor->toPlainText();
        const qint64 sizeBytes = text.toUtf8().size();
        const DocumentLimits limits = DocumentLimits::forSize(sizeBytes);
        const bool tierChanged = (limits.tier() != m_limits.tier());
        m_limits = limits;
        if (tierChanged) {
            m_viewer->setDocumentLimits(m_limits);
            m_editor->setDocumentLimits(m_limits);
        }
        updateStatusBar();
        updateDocStats();
        m_viewer->setMarkdown(text);
    });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (m_loading)
            return;
        updateCursorStatus();
        updateDocStats();
    });
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        m_viewer->scheduleRerender();
    });
    connect(m_viewer, &MarkdownView::rendered, this, &MainWindow::updateRenderStatus);

    // Click-to-source: a click in the preview sends the editor's caret to the
    // line it came from (Zed-style). The two panes scroll independently on
    // purpose: rendered and source heights do not line up, so keeping them in
    // step by scroll fraction only ever disagrees with the reader.
    connect(m_viewer, &MarkdownView::sourceLineActivated, m_editor, &MarkdownEditor::gotoSourceLine);
    connect(m_viewer, &MarkdownView::taskCheckboxToggled, m_editor, &MarkdownEditor::toggleTaskCheckboxAtSourceLine);

    buildSidebar();

    m_viewer->setMarkdown(m_editor->toPlainText());
    updateStatusBar();
}

void MainWindow::buildSidebar()
{
    m_outlinePanel = new OutlinePanel(this);
    m_filesPanel = new FileTreePanel(this);

    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->addTab(m_outlinePanel, tr("Outline"));
    tabs->addTab(m_filesPanel, tr("Files"));

    m_sidebar = new QDockWidget(tr("Sidebar"), this);
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    m_sidebar->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidebarTitleBar = new SidebarTitleBar(m_sidebar, tr("Sidebar"), m_sidebar);
    m_sidebar->setTitleBarWidget(m_sidebarTitleBar);
    m_sidebar->setWidget(tabs);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebar);
    resizeDocks({m_sidebar}, {240}, Qt::Horizontal);

    connect(m_viewer, &MarkdownView::rendered, this, &MainWindow::refreshOutline);
    connect(m_viewer, &MarkdownView::visibleHeadingChanged, this, [this](int block) {
        // A pinned bar stays on the row that was clicked; it follows the
        // reading position again once the reader scrolls (releaseOutlinePin).
        if (m_outlinePin >= 0)
            return;
        m_outlinePanel->setCurrentBlock(block);
    });
    connect(m_viewer, &MarkdownView::userScrolled, this, &MainWindow::releaseOutlinePin);
    connect(m_outlinePanel, &OutlinePanel::entryActivated, this, &MainWindow::activateOutlineEntry);
    connect(m_filesPanel, &FileTreePanel::fileActivated, this, [this](const QString &path) {
        if (maybeSave())
            openFile(path);
    });
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New"), QKeySequence::New, this, &MainWindow::newFile);
    fileMenu->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::openFileDialog);
    fileMenu->addAction(tr("Open F&older…"), QKeySequence(QStringLiteral("Ctrl+Shift+O")), this, &MainWindow::openFolderDialog);
    m_recentFilesMenu = fileMenu->addMenu(tr("Open &Recent"));
    updateRecentFilesMenu();
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::saveFile);
    fileMenu->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, &MainWindow::saveFileAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence(QStringLiteral("Ctrl+Q")), this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Find…"), QKeySequence::Find, this, [this] {
        m_find->openForFocusedPane();
    });
    editMenu->addAction(tr("&Replace…"), QKeySequence::Replace, this, [this] {
        m_find->openReplaceForFocusedPane();
    });
    editMenu->addAction(tr("Find &Next"), QKeySequence(QStringLiteral("F3")), this, [this] {
        m_find->findNext();
    });
    editMenu->addAction(tr("Find &Previous"), QKeySequence(QStringLiteral("Shift+F3")), this, [this] {
        m_find->findPrevious();
    });

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

    QAction *sidebarAction = m_sidebar->toggleViewAction();
    sidebarAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    viewMenu->addAction(sidebarAction);
    viewMenu->addSeparator();

    m_toggleEditor = viewMenu->addAction(tr("&Editor"));
    m_toggleEditor->setCheckable(true);
    // The shipped default is a viewer: the sidebar opens for reading, and the
    // editor is one F9 away. The user's last layout is restored below.
    m_toggleEditor->setChecked(false);
    m_toggleEditor->setShortcut(QStringLiteral("F9"));
    connect(m_toggleEditor, &QAction::toggled, this, [this](bool checked) {
        m_editorPane->setVisible(checked);
        persistPaneSettings();
    });

    m_toggleViewer = viewMenu->addAction(tr("&Preview"));
    m_toggleViewer->setCheckable(true);
    m_toggleViewer->setChecked(true);
    m_toggleViewer->setShortcut(QStringLiteral("F10"));
    connect(m_toggleViewer, &QAction::toggled, this, [this](bool checked) {
        m_viewerPane->setVisible(checked);
        persistPaneSettings();
    });

    m_toggleWordWrap = viewMenu->addAction(tr("&Word Wrap"), this, [this](bool checked) {
        toggleWordWrap(checked);
    });
    m_toggleWordWrap->setCheckable(true);
    m_toggleWordWrap->setShortcut(QKeySequence(QStringLiteral("Alt+Z")));

    restorePaneSettings();

    m_toggleRemoteImages = viewMenu->addAction(tr("Load &Remote Images"));
    m_toggleRemoteImages->setObjectName(QStringLiteral("toggleRemoteImages"));
    m_toggleRemoteImages->setCheckable(true);
    connect(m_toggleRemoteImages, &QAction::toggled, this, [this](bool checked) {
        QSettings().setValue(QStringLiteral("view/remoteImages"), checked);
        m_viewer->setRemoteImagesEnabled(checked);
    });
    restoreRemoteImageSettings();

    viewMenu->addSeparator();
    QAction *zoomInAction = viewMenu->addAction(tr("Zoom &In"), this, &MainWindow::zoomIn);
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    QAction *zoomOutAction = viewMenu->addAction(tr("Zoom &Out"), this, &MainWindow::zoomOut);
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    QAction *resetZoomAction = viewMenu->addAction(tr("Reset &Zoom"), this, &MainWindow::resetZoom);
    resetZoomAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));

    m_fullScreenAction = viewMenu->addAction(tr("&Full Screen"), this, [this] {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(QStringLiteral("F11")));

    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
    auto *group = new QActionGroup(this);
    struct ThemeChoice {
        QString label;
        ThemeManager::Mode mode;
    };
    const std::array<ThemeChoice, 4> modes = {{
        {tr("Follow &System"), ThemeManager::Mode::System},
        {tr("&Light"), ThemeManager::Mode::Light},
        {tr("&Dark"), ThemeManager::Mode::Dark},
        {tr("&Custom"), ThemeManager::Mode::Custom},
    }};
    for (const ThemeChoice &entry : modes) {
        QAction *action = group->addAction(entry.label);
        action->setCheckable(true);
        if (entry.mode == m_themes->mode())
            action->setChecked(true);
        const ThemeManager::Mode mode = entry.mode;
        connect(action, &QAction::toggled, this, [this, mode](bool checked) {
            if (checked)
                m_themes->setMode(mode);
        });
        themeMenu->addAction(action);
        m_themeModeActions.insert(int(entry.mode), action);
        if (entry.mode == ThemeManager::Mode::Custom) {
            m_customThemeAction = action;
            action->setEnabled(false); // enabled once a custom theme file is loaded
        }
    }
    themeMenu->addSeparator();
    themeMenu->addAction(tr("Load Theme &File…"), this, &MainWindow::loadThemeFromFile);

    // Discovered desktop colour schemes (e.g. KDE Plasma *.colors), if any.
    const QVector<SystemThemeProvider::Scheme> schemes = SystemThemeProvider::availableSchemes();
    if (!schemes.isEmpty()) {
        QMenu *systemMenu = themeMenu->addMenu(tr("System &Schemes"));
        for (const SystemThemeProvider::Scheme &scheme : schemes) {
            const QString path = scheme.path;
            QAction *action = systemMenu->addAction(scheme.name);
            connect(action, &QAction::triggered, this, [this, path] {
                if (const std::optional<ThemeDefinition> theme = SystemThemeProvider::loadScheme(path)) {
                    m_themes->setCustomTheme(*theme);
                    m_themes->setMode(ThemeManager::Mode::Custom);
                    m_customThemeAction->setEnabled(true);
                    m_customThemeAction->setChecked(true);
                    persistThemeSettings();
                }
            });
        }
    }

    // Pick the faces out of the ones installed on this machine; the bundled
    // pair stays the default and one click brings it back.
    viewMenu->addAction(tr("&Fonts…"), this, &MainWindow::chooseFonts);
}

void MainWindow::applyTheme(const ThemeDefinition &theme)
{
    QApplication::setPalette(theme.palette);

    // The dock separator is a thick bevel by default; a hairline keeps the
    // sidebar and the editor reading as two flat panes. The sidebar's own title
    // bar follows the palette on its own.
    setStyleSheet(QStringLiteral("QMainWindow::separator { background: %1; width: 1px; height: 1px; }"
                                 "QTabBar::tab { padding: 5px 12px; border: none; background: transparent; color: %2; }"
                                 "QTabBar::tab:selected { border-bottom: 2px solid %3; font-weight: bold; color: %4; }"
                                 "QStatusBar { border-top: 1px solid %1; }"
                                 "QStatusBar QLabel { padding: 0 4px; }")
                      .arg(theme.markdown.rule.name(),
                           theme.markdown.weakText.name(),
                           theme.palette.color(QPalette::Highlight).name(),
                           theme.palette.color(QPalette::WindowText).name()));

    m_editor->setTheme(theme);

    RenderStyle style = m_viewer->renderStyle();
    style.theme = theme;
    m_viewer->setRenderStyle(style);
}

void MainWindow::applyFileContent(const QString &content, const QString &path)
{
    m_loading = true;

    // Whatever was being searched for is now at different offsets, in a
    // different file.
    m_find->reset();

    // Decide up front how much work this document is worth, before either pane
    // sees it: the limits change what setMarkdown() does.
    m_limits = DocumentLimits::forSize(content.toUtf8().size());
    m_viewer->setDocumentLimits(m_limits);
    m_editor->setDocumentLimits(m_limits);

    m_viewer->setBaseDir(path.isEmpty() ? QString() : QFileInfo(path).absolutePath());
    m_editor->setMarkdown(content);
    m_editor->document()->setModified(false);
    // Opening a file is not typing: skip the debounce and render once now, so
    // the preview is never left stale for the length of a typing pause.
    m_viewer->setMarkdown(content);
    m_viewer->renderNow();
    m_filesPanel->setCurrentFile(path);
    updateStatusBar();
    updateCursorStatus();
    updateDocStats();
    updateWindowTitle();

    m_loading = false;
}

void MainWindow::updateStatusBar()
{
    const QString size = QLocale().formattedDataSize(m_limits.sizeBytes());
    switch (m_limits.tier()) {
    case DocumentLimits::Tier::Interactive:
        m_statusInfo->setText(m_limits.sizeBytes() > 0 ? size : QString());
        break;
    case DocumentLimits::Tier::Deferred:
        m_statusInfo->setText(tr("%1 · large file: the preview updates when typing pauses").arg(size));
        break;
    case DocumentLimits::Tier::Manual:
        m_statusInfo->setText(tr("%1 · preview paused: too large to redraw while typing").arg(size));
        break;
    }
}

void MainWindow::updateCursorStatus()
{
    if (!m_statusCursor)
        return;
    const QTextCursor cursor = m_editor->textCursor();
    const int line = cursor.blockNumber() + 1;
    const int col = cursor.positionInBlock() + 1;
    m_statusCursor->setText(tr("Ln %1, Col %2").arg(line).arg(col));
}

void MainWindow::updateDocStats()
{
    if (!m_statusStats)
        return;
    const QString text = m_editor->toPlainText();
    const int charCount = int(text.length());
    int wordCount = 0;
    bool inWord = false;
    for (const QChar &ch : text) {
        if (ch.isSpace()) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++wordCount;
        }
    }
    const QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        const int selChars = int(cursor.selectedText().length());
        m_statusStats->setText(tr("%1 words · %2 chars (%3 sel)").arg(wordCount).arg(charCount).arg(selChars));
    } else {
        m_statusStats->setText(tr("%1 words · %2 chars").arg(wordCount).arg(charCount));
    }
}

void MainWindow::toggleWordWrap(bool wrap)
{
    m_editor->setWordWrap(wrap);
    QSettings().setValue(QStringLiteral("editor/wordWrap"), wrap);
    if (m_statusWrap)
        m_statusWrap->setText(wrap ? tr("Wrap: On") : tr("Wrap: Off"));
}

void MainWindow::restoreWordWrapSettings()
{
    const bool wrap = QSettings().value(QStringLiteral("editor/wordWrap"), false).toBool();
    if (m_toggleWordWrap)
        m_toggleWordWrap->setChecked(wrap);
    m_editor->setWordWrap(wrap);
    if (m_statusWrap)
        m_statusWrap->setText(wrap ? tr("Wrap: On") : tr("Wrap: Off"));
}

void MainWindow::zoomIn()
{
    m_editor->zoomIn(1);
    m_viewer->zoomIn(1);
}

void MainWindow::zoomOut()
{
    m_editor->zoomOut(1);
    m_viewer->zoomOut(1);
}

void MainWindow::resetZoom()
{
    const QString body = QSettings().value(QStringLiteral("fonts/bodyFamily")).toString();
    const QString code = QSettings().value(QStringLiteral("fonts/codeFamily")).toString();
    applyFontChoice(body, code);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (m_fullScreenAction)
            m_fullScreenAction->setChecked(isFullScreen());
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::updateRecentFilesMenu()
{
    if (!m_recentFilesMenu)
        return;

    m_recentFilesMenu->clear();
    const QStringList recent = QSettings().value(QStringLiteral("history/recentFiles")).toStringList();
    int count = 0;
    for (const QString &path : recent) {
        if (!QFileInfo::exists(path))
            continue;
        const QString text = QFileInfo(path).fileName() + QStringLiteral(" (") + QDir::toNativeSeparators(path) + QStringLiteral(")");
        QAction *action = m_recentFilesMenu->addAction(text, this, [this, path] {
            if (maybeSave())
                openFile(path);
        });
        action->setData(path);
        ++count;
    }

    if (count == 0) {
        QAction *emptyAction = m_recentFilesMenu->addAction(tr("No Recent Files"));
        emptyAction->setEnabled(false);
    } else {
        m_recentFilesMenu->addSeparator();
        m_recentFilesMenu->addAction(tr("Clear Recent Files"), this, &MainWindow::clearRecentFiles);
    }
}

void MainWindow::addRecentFile(const QString &path)
{
    if (path.isEmpty())
        return;

    const QString clean = QDir::cleanPath(path);
    QStringList recent = QSettings().value(QStringLiteral("history/recentFiles")).toStringList();
    recent.removeAll(clean);
    recent.prepend(clean);
    while (recent.size() > 10)
        recent.removeLast();
    QSettings().setValue(QStringLiteral("history/recentFiles"), recent);
    updateRecentFilesMenu();
}

void MainWindow::clearRecentFiles()
{
    QSettings().remove(QStringLiteral("history/recentFiles"));
    updateRecentFilesMenu();
}

void MainWindow::updateRenderStatus()
{
    if (m_viewer->previewPaused()) {
        m_statusRender->setText(tr("preview paused"));
        return;
    }
    m_statusRender->setText(tr("rendered in %1 ms").arg(m_viewer->lastRenderMs()));
}

void MainWindow::openFolderDialog()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Open Folder"), m_filesPanel->rootDirectory());
    if (!path.isEmpty())
        setFolder(path);
}

void MainWindow::setFolder(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return;

    m_filesPanel->setRootDirectory(path);
    QSettings().setValue(QStringLiteral("files/folder"), path);
}

void MainWindow::refreshOutline()
{
    m_outline = Outline::fromDocument(*m_viewer->document());
    m_outlinePanel->setOutline(m_outline);
    if (m_outlinePin >= 0 && m_outlinePin < m_outline.size())
        m_outlinePanel->setCurrentBlock(m_outline.at(m_outlinePin).blockNumber);
    else
        m_outlinePanel->setCurrentBlock(m_viewer->visibleHeading());
}

void MainWindow::releaseOutlinePin()
{
    if (m_outlinePin < 0)
        return;
    m_outlinePin = -1;
    m_outlinePanel->setCurrentBlock(m_viewer->visibleHeading());
}

void MainWindow::activateOutlineEntry(int index)
{
    if (index < 0 || index >= m_outline.size())
        return;

    const OutlineEntry &entry = m_outline.at(index);
    // The bar belongs to the clicked row from now on: the jump reports its own
    // target, and organic updates stay ignored until the reader scrolls.
    if (m_viewer->scrollToBlock(entry.blockNumber))
        m_outlinePin = index;

    // The editor locates headings by scanning the source, which can disagree
    // with the renderer on exotic documents. Jumping somewhere wrong would be
    // worse than not jumping at all, so the two lists have to agree first.
    if (Outline::headingSourceLines(m_editor->toPlainText()).size() == m_outline.size())
        m_editor->gotoHeading(index);
}

void MainWindow::updateWindowTitle()
{
    const QString name = m_file->filePath().isEmpty() ? tr("Untitled") : QFileInfo(m_file->filePath()).fileName();
    const QString modified = m_editor->document()->isModified() ? QStringLiteral("*") : QString();
    setWindowTitle(tr("%1%2 — Total Markdown").arg(modified, name));
}

bool MainWindow::openFile(const QString &path)
{
    QString error;
    if (!m_file->load(path, &error)) {
        QMessageBox::warning(this, tr("Open File"), tr("Cannot open %1:\n%2").arg(path, error));
        return false;
    }
    applyFileContent(m_file->content(), m_file->filePath());
    addRecentFile(m_file->filePath());
    return true;
}

void MainWindow::setSidebarVisible(bool visible)
{
    if (visible) {
        if (auto *tabs = qobject_cast<QTabWidget *>(m_sidebar->widget()))
            tabs->setCurrentIndex(0); // the Outline tab, not Files
    }
    m_sidebar->setVisible(visible);
}

void MainWindow::openFind(const QString &query)
{
    // The controller opens the bar of the pane that owns the keyboard focus,
    // so put the caret side in front before asking for the strip.
    m_editor->setFocus();
    m_find->openForFocusedPane();
    m_editorBar->setQuery(query); // FindBar runs the search as the user types
}

void MainWindow::openFileDialog()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Markdown"), QString(), tr("Markdown files (*.md *.markdown *.mkd);;All files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;
    m_file->close();
    applyFileContent(QString(), QString());
}

bool MainWindow::saveFile()
{
    if (m_file->filePath().isEmpty())
        return saveFileAs();

    QString error;
    if (!m_file->save(m_file->filePath(), m_editor->toPlainText(), &error)) {
        QMessageBox::warning(this, tr("Save File"), tr("Cannot save %1:\n%2").arg(m_file->filePath(), error));
        return false;
    }
    m_editor->document()->setModified(false);
    updateWindowTitle();
    addRecentFile(m_file->filePath());
    return true;
}

bool MainWindow::saveFileAs()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Markdown"), m_file->filePath(), tr("Markdown files (*.md);;All files (*)"));
    if (path.isEmpty())
        return false;

    QString error;
    if (!m_file->save(path, m_editor->toPlainText(), &error)) {
        QMessageBox::warning(this, tr("Save File"), tr("Cannot save %1:\n%2").arg(path, error));
        return false;
    }
    m_editor->document()->setModified(false);
    m_viewer->setBaseDir(QFileInfo(path).absolutePath());
    updateWindowTitle();
    addRecentFile(path);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(this,
                                                                    tr("Total Markdown"),
                                                                    tr("The document has unsaved changes.\nDo you want to save them?"),
                                                                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveFile();
    return answer != QMessageBox::Cancel;
}

void MainWindow::onExternalChange()
{
    if (m_editor->document()->isModified()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("File Changed"),
            tr("%1 changed on disk, but you have unsaved edits.\nReload and lose your edits?").arg(QFileInfo(m_file->filePath()).fileName()),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    applyFileContent(m_file->content(), m_file->filePath());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void MainWindow::restoreThemeSettings()
{
    QSettings settings;

    const QString customJson = settings.value(QStringLiteral("theme/custom")).toString();
    if (!customJson.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(customJson.toUtf8());
        if (document.isObject()) {
            if (const std::optional<ThemeDefinition> theme = ThemeDefinition::fromJson(document.object())) {
                m_themes->setCustomTheme(*theme);
                m_customThemeAction->setEnabled(true);
            }
        }
    }

    const QString mode = settings.value(QStringLiteral("theme/mode")).toString();
    if (!mode.isEmpty())
        m_themes->setMode(modeFromName(mode));
}

void MainWindow::persistThemeSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("theme/mode"), modeName(m_themes->mode()));
    if (m_themes->mode() == ThemeManager::Mode::Custom) {
        const QJsonDocument document(m_themes->currentTheme().toJson());
        settings.setValue(QStringLiteral("theme/custom"), QString::fromUtf8(document.toJson(QJsonDocument::Compact)));
    }
}

// Pane visibility follows the theme: absent keys mean the shipped default,
// which is a viewer-only window.
void MainWindow::restorePaneSettings()
{
    QSettings settings;
    const bool editor = settings.value(QStringLiteral("view/editorVisible"), false).toBool();
    const bool viewer = settings.value(QStringLiteral("view/viewerVisible"), true).toBool();

    m_toggleEditor->setChecked(editor);
    m_toggleViewer->setChecked(viewer);
    m_editorPane->setVisible(editor);
    m_viewerPane->setVisible(viewer);
}

void MainWindow::persistPaneSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("view/editorVisible"), m_toggleEditor->isChecked());
    settings.setValue(QStringLiteral("view/viewerVisible"), m_toggleViewer->isChecked());
}

// Remote images are on by default: a document full of shields.io badges and a
// GitHub-hosted banner looks right out of the box. The setting is here for a
// reader who would rather not touch the network at all.
void MainWindow::restoreRemoteImageSettings()
{
    QSettings settings;
    const bool enabled = settings.value(QStringLiteral("view/remoteImages"), true).toBool();
    m_toggleRemoteImages->setChecked(enabled); // fires the handler, applying it
    m_viewer->setRemoteImagesEnabled(enabled);
}

void MainWindow::restoreFontSettings()
{
    QSettings settings;
    applyFontChoice(settings.value(QStringLiteral("fonts/body")).toString(), settings.value(QStringLiteral("fonts/code")).toString());
}

void MainWindow::applyFontChoice(const QString &bodyFamily, const QString &codeFamily)
{
    QSettings settings;
    if (bodyFamily.isEmpty() && codeFamily.isEmpty()) {
        settings.remove(QStringLiteral("fonts"));
    } else {
        settings.setValue(QStringLiteral("fonts/body"), bodyFamily);
        settings.setValue(QStringLiteral("fonts/code"), codeFamily);
    }

    FontManager fonts;
    if (!bodyFamily.isEmpty() || !codeFamily.isEmpty()) {
        fonts.setFontSet(FontManager::FontSet::Custom);
        fonts.setCustomFonts(bodyFamily, codeFamily);
    }

    RenderStyle style = m_viewer->renderStyle();
    style.fonts = fonts;
    m_viewer->setRenderStyle(style);
    m_editor->setCodeFont(fonts.codeFont());
}

void MainWindow::chooseFonts()
{
    FontsDialog dialog(this);
    // Offer what is on screen right now, so cancelling changes nothing.
    const RenderStyle &style = m_viewer->renderStyle();
    dialog.setBodyFamily(QFontInfo(style.fonts.bodyFont()).family());
    dialog.setCodeFamily(QFontInfo(style.fonts.codeFont()).family());
    if (dialog.exec() != QDialog::Accepted)
        return;

    applyFontChoice(dialog.bodyFamily(), dialog.codeFamily());
}

void MainWindow::loadThemeFromFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Theme"), QString(), tr("Theme files (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Load Theme"), tr("Cannot open %1.").arg(path));
        return;
    }

    QJsonParseError error = {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, tr("Load Theme"), tr("%1 is not a valid theme file: %2").arg(path, error.errorString()));
        return;
    }

    const std::optional<ThemeDefinition> theme = ThemeDefinition::fromJson(document.object());
    if (!theme) {
        QMessageBox::warning(this, tr("Load Theme"), tr("%1 is missing required theme fields.").arg(path));
        return;
    }

    m_themes->setCustomTheme(*theme);
    m_themes->setMode(ThemeManager::Mode::Custom);
    m_customThemeAction->setEnabled(true);
    m_customThemeAction->setChecked(true);
    persistThemeSettings();
}