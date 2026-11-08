// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>

#include "app/MainWindow.h"
#include "editor/MarkdownEditor.h"
#include "viewer/MarkdownView.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextCursor>

class AppTest : public QObject
{
    Q_OBJECT

private slots:
    void windowOpensWithViewerOnlyByDefault();
    void panelsCanBeClosedAndReopened();
    void paneLayoutPersistsAcrossWindows();
    void fontsApplyToPreviewAndEditor();
    void fontChoicePersistsAcrossWindows();
    void themeChangeUpdatesApplicationPalette();
    void typingUpdatesPreview();
    void openingFileLoadsContentAndTitle();
    void savingFileWritesToDisk();
    void externalChangeReloadsUnmodifiedDocument();
    void themeModePersistsAcrossWindows();
    void hugeFileOpensPausedAndQuickly();
    void openingAFileRendersThePreviewWithoutWaiting();
    void remoteImagesAreOnByDefaultAndPersistAcrossWindows();

private:
    static void typeInto(MarkdownEditor *editor, const QString &text, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
};

void AppTest::typeInto(MarkdownEditor *editor, const QString &text, QTextCursor::MoveMode)
{
    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
}

void AppTest::openingAFileRendersThePreviewWithoutWaiting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("chapter.md"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Chapter\n\nbody\n");
    }

    MainWindow window;
    // A long debounce stands in for the typing-pause path: opening a file must
    // not wait for it, or the preview sits stale while the reader looks at it.
    window.viewer()->setRenderInterval(5000);
    QVERIFY(window.openFile(path));

    QVERIFY(window.viewer()->document()->toPlainText().contains(QStringLiteral("Chapter")));
}

void AppTest::typingUpdatesPreview()
{
    MainWindow window;
    window.viewer()->setRenderInterval(0);

    typeInto(window.editor(), QStringLiteral("# Hello\n\nworld\n"));

    const QString text = window.viewer()->document()->toPlainText();
    QVERIFY(text.contains(QStringLiteral("Hello")));
    QVERIFY(text.contains(QStringLiteral("world")));
}

void AppTest::openingFileLoadsContentAndTitle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("note.md"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# From disk\n");
    }

    MainWindow window;
    window.viewer()->setRenderInterval(0);
    QVERIFY(window.openFile(path));

    QCOMPARE(window.editor()->toPlainText(), QStringLiteral("# From disk\n"));
    QVERIFY(window.viewer()->document()->toPlainText().contains(QStringLiteral("From disk")));
    QVERIFY(window.windowTitle().contains(QStringLiteral("note.md")));
    QVERIFY(!window.windowTitle().startsWith(QLatin1Char('*')));
}

void AppTest::savingFileWritesToDisk()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("save.md"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("initial\n");
    }

    MainWindow window;
    window.viewer()->setRenderInterval(0);
    QVERIFY(window.openFile(path)); // remembers the file path
    window.editor()->setMarkdown(QStringLiteral("written\n"));
    window.editor()->document()->setModified(true);
    QVERIFY(window.saveFile());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("written\n"));
    QVERIFY(!window.editor()->document()->isModified());
}

void AppTest::externalChangeReloadsUnmodifiedDocument()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("live.md"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("first\n");
    }

    MainWindow window;
    window.viewer()->setRenderInterval(0);
    QVERIFY(window.openFile(path));
    QVERIFY(!window.editor()->document()->isModified());

    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("second\n");
        file.flush();
        file.close();
    }

    QTRY_VERIFY_WITH_TIMEOUT(window.editor()->toPlainText() == QStringLiteral("second\n"), 8000);
    QVERIFY(window.viewer()->document()->toPlainText().contains(QStringLiteral("second")));
}

void AppTest::themeChangeUpdatesApplicationPalette()
{
    MainWindow window;
    window.themeManager()->setMode(ThemeManager::Mode::Dark);
    QCOMPARE(QApplication::palette().color(QPalette::Window), ThemeDefinition::dark().palette.color(QPalette::Window));

    window.themeManager()->setMode(ThemeManager::Mode::Light);
    QCOMPARE(QApplication::palette().color(QPalette::Window), ThemeDefinition::light().palette.color(QPalette::Window));
}

void AppTest::windowOpensWithViewerOnlyByDefault()
{
    QSettings().remove(QStringLiteral("view"));
    MainWindow window;
    window.show();

    QVERIFY(window.viewerPanel()->isVisible());
    QVERIFY(!window.editorPanel()->isVisible());
    QVERIFY(!window.toggleEditorAction()->isChecked());
    QVERIFY(window.toggleViewerAction()->isChecked());
}

void AppTest::panelsCanBeClosedAndReopened()
{
    QSettings().remove(QStringLiteral("view"));
    MainWindow window;
    window.show();

    // The editor starts hidden; its toggle brings it back alongside the viewer.
    window.toggleEditorAction()->trigger();
    QVERIFY(window.editorPanel()->isVisible());
    QVERIFY(window.viewerPanel()->isVisible());

    window.toggleViewerAction()->trigger();
    QVERIFY(!window.viewerPanel()->isVisible());

    // An editor-only window is a legal state; back to the shipped default.
    window.toggleViewerAction()->trigger();
    window.toggleEditorAction()->trigger();
    QVERIFY(!window.editorPanel()->isVisible());
    QVERIFY(window.viewerPanel()->isVisible());
}

void AppTest::paneLayoutPersistsAcrossWindows()
{
    QSettings settings;
    settings.remove(QStringLiteral("view"));

    {
        MainWindow window;
        window.show();
        window.toggleEditorAction()->trigger(); // the default is viewer-only
        QVERIFY(window.editorPanel()->isVisible());
    }
    // A fresh window must pick the saved layout back up.
    {
        MainWindow window;
        window.show();
        QVERIFY(window.editorPanel()->isVisible());
        QVERIFY(window.viewerPanel()->isVisible());

        window.toggleViewerAction()->trigger(); // editor-only
    }
    {
        MainWindow window;
        window.show();
        QVERIFY(window.editorPanel()->isVisible());
        QVERIFY(!window.viewerPanel()->isVisible());
    }

    settings.remove(QStringLiteral("view"));
}

void AppTest::fontsApplyToPreviewAndEditor()
{
    QSettings().remove(QStringLiteral("fonts"));
    MainWindow window;
    window.show();

    window.applyFontChoice(QStringLiteral("Georgia"), QStringLiteral("Courier New"));

    const RenderStyle &style = window.viewer()->renderStyle();
    QCOMPARE(style.fonts.bodyFont().family(), QStringLiteral("Georgia"));
    QCOMPARE(style.fonts.codeFont().family(), QStringLiteral("Courier New"));
    QCOMPARE(window.editor()->font().family(), QStringLiteral("Courier New"));

    // A blank pair is the way back to the bundled faces.
    window.applyFontChoice(QString(), QString());
    QCOMPARE(window.viewer()->renderStyle().fonts.bodyFont().family(), QStringLiteral("Inter"));
    QCOMPARE(window.editor()->font().family(), QStringLiteral("JetBrains Mono"));
}

void AppTest::fontChoicePersistsAcrossWindows()
{
    QSettings settings;
    settings.remove(QStringLiteral("fonts"));

    {
        MainWindow window;
        window.applyFontChoice(QStringLiteral("Georgia"), QStringLiteral("Courier New"));
    }
    // A fresh window must typeset with the saved families.
    {
        MainWindow window;
        QCOMPARE(window.viewer()->renderStyle().fonts.bodyFont().family(), QStringLiteral("Georgia"));
        QCOMPARE(window.viewer()->renderStyle().fonts.codeFont().family(), QStringLiteral("Courier New"));
        QCOMPARE(window.editor()->font().family(), QStringLiteral("Courier New"));
    }

    settings.remove(QStringLiteral("fonts"));
}

void AppTest::themeModePersistsAcrossWindows()
{
    QSettings settings;
    settings.clear();

    {
        MainWindow window;
        window.themeManager()->setMode(ThemeManager::Mode::Dark);
    }
    // A fresh window must pick the saved mode back up.
    {
        MainWindow window;
        QCOMPARE(window.themeManager()->mode(), ThemeManager::Mode::Dark);
        QCOMPARE(QApplication::palette().color(QPalette::Window), ThemeDefinition::dark().palette.color(QPalette::Window));
    }

    settings.clear();
}

void AppTest::hugeFileOpensPausedAndQuickly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("big.md"));

    QByteArray chunk;
    for (int i = 0; i < 64; ++i)
        chunk += "# Heading\n\nA prose paragraph with enough words to look like a real document.\n\n";
    const qsizetype repeats = (6 * 1024 * 1024) / chunk.size() + 1;

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (qsizetype i = 0; i < repeats; ++i)
        file.write(chunk);
    file.close();
    QVERIFY(QFileInfo(path).size() > 5 * 1024 * 1024);

    MainWindow window;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(window.openFile(path));
    const qint64 elapsed = timer.elapsed();

    // The preview must not even attempt the render: it would take seconds and
    // the user would be staring at a frozen window.
    QVERIFY(window.viewer()->previewPaused());
    QVERIFY(window.viewer()->document()->toPlainText().contains(QStringLiteral("Preview paused")));
    QVERIFY(!window.editor()->highlightingEnabled());
    // Loading and laying out the source may cost something, but not a freeze.
    QVERIFY2(elapsed < 8000, qPrintable(QStringLiteral("opening took %1 ms").arg(elapsed)));
}

void AppTest::remoteImagesAreOnByDefaultAndPersistAcrossWindows()
{
    QSettings settings;
    settings.remove(QStringLiteral("view/remoteImages"));

    {
        MainWindow window;
        window.show();
        QAction *toggle = window.findChild<QAction *>(QStringLiteral("toggleRemoteImages"));
        QVERIFY(toggle);
        QVERIFY(toggle->isChecked());
        QVERIFY(window.viewer()->remoteImagesEnabled());

        toggle->trigger(); // turn remote loading off
        QVERIFY(!window.viewer()->remoteImagesEnabled());
    }
    {
        MainWindow window;
        window.show();
        QAction *toggle = window.findChild<QAction *>(QStringLiteral("toggleRemoteImages"));
        QVERIFY(toggle);
        QVERIFY(!toggle->isChecked());
        QVERIFY(!window.viewer()->remoteImagesEnabled());
    }

    settings.remove(QStringLiteral("view/remoteImages"));
}

QTEST_MAIN(AppTest)
#include "test_app.moc"
