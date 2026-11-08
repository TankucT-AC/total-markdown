// SPDX-FileCopyrightText: 2026 Total Markdown contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/FileDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

FileDocument::FileDocument(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &FileDocument::handleFileChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileDocument::handleDirectoryChanged);
}

bool FileDocument::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    m_content = QString::fromUtf8(file.readAll());
    m_path = QFileInfo(path).absoluteFilePath();
    m_directory = QFileInfo(m_path).absolutePath();
    armWatch();
    return true;
}

bool FileDocument::save(const QString &path, const QString &content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.close();

    m_saving = true;
    m_content = content;
    m_path = QFileInfo(path).absoluteFilePath();
    m_directory = QFileInfo(m_path).absolutePath();
    armWatch();
    m_saving = false;
    return true;
}

void FileDocument::close()
{
    unwatchAll();
    m_path.clear();
    m_directory.clear();
    m_content.clear();
}

QString FileDocument::filePath() const
{
    return m_path;
}

QString FileDocument::content() const
{
    return m_content;
}

bool FileDocument::exists() const
{
    return !m_path.isEmpty() && QFileInfo::exists(m_path);
}

void FileDocument::armWatch()
{
    unwatchAll();
    if (!m_path.isEmpty() && QFileInfo::exists(m_path))
        m_watcher.addPath(m_path);
    if (!m_directory.isEmpty() && QFileInfo::exists(m_directory))
        m_watcher.addPath(m_directory);
}

void FileDocument::unwatchAll()
{
    const QStringList files = m_watcher.files();
    if (!files.isEmpty())
        m_watcher.removePaths(files);
    const QStringList directories = m_watcher.directories();
    if (!directories.isEmpty())
        m_watcher.removePaths(directories);
}

void FileDocument::handleFileChanged()
{
    handleDirectoryChanged();
}

void FileDocument::handleDirectoryChanged()
{
    QFile file(m_path);
    bool opened = file.open(QIODevice::ReadOnly);
    // On Windows, the file might still be briefly held by the external writer
    // when the directory change notification fires. Retry for up to 100 ms.
    for (int retry = 0; !opened && retry < 5; ++retry) {
        QThread::msleep(20);
        opened = file.open(QIODevice::ReadOnly);
    }

    if (!opened) {
        // The file is gone (deleted or being atomically replaced).
        rearmWatch();
        emit externallyChanged();
        return;
    }

    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    rearmWatch();

    if (m_saving || content == m_content)
        return; // our own write, or a no-op touch

    m_content = content;
    emit externallyChanged();
}

void FileDocument::rearmWatch()
{
    // A replaced/deleted file drops its watch; re-add file and directory.
    if (!m_watcher.files().contains(m_path))
        m_watcher.addPath(m_path);
    if (!m_watcher.directories().contains(m_directory))
        m_watcher.addPath(m_directory);
}