#include "DownloadTempCleanup.h"

#include "ConfigManager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
const QRegularExpression &uuidPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"));
    return pattern;
}

bool isUuidDirectoryName(const QString &name)
{
    return uuidPattern().match(name).hasMatch();
}

bool isOwnedDirectory(const QString &id, const QString &candidatePath, QFileInfo *infoOut = nullptr)
{
    if (id.isEmpty() || candidatePath.isEmpty()) {
        return false;
    }

    const QFileInfo info(QDir::fromNativeSeparators(candidatePath));
    if (!info.exists() || !info.isDir() || info.isSymLink() || info.fileName().compare(id, Qt::CaseSensitive) != 0) {
        return false;
    }

    if (infoOut) {
        *infoOut = info;
    }
    return true;
}
}

namespace DownloadTempCleanup {

QString resolveRoot(const ConfigManager *configManager)
{
    QString root;
    if (configManager) {
        root = configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString().trimmed();
        if (root.isEmpty()) {
            const QString completed = configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString().trimmed();
            if (!completed.isEmpty()) {
                root = QDir(completed).filePath(QStringLiteral("temp_downloads"));
            }
        }
    }

    if (root.isEmpty()) {
        root = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("LzyDownloader"));
    }
    return QDir(root).absolutePath();
}

QString pathForId(const QString &root, const QString &id)
{
    return QDir(root).filePath(id);
}

bool removeOwnedDirectory(const QString &id, const QString &candidatePath)
{
    QFileInfo info;
    if (!isOwnedDirectory(id, candidatePath, &info)) {
        qWarning() << "Refusing to remove unexpected temporary directory for" << id << ":" << candidatePath;
        return false;
    }

    QDir directory(info.absoluteFilePath());
    if (!directory.removeRecursively()) {
        qWarning() << "Failed to remove temporary UUID directory for" << id << ":" << info.absoluteFilePath();
        return false;
    }

    qDebug() << "Removed terminal download temporary directory:" << info.absoluteFilePath();
    return true;
}

bool removeEmptyOwnedDirectory(const QString &id, const QString &candidatePath)
{
    QFileInfo info;
    if (!isOwnedDirectory(id, candidatePath, &info)) {
        return false;
    }

    QDir directory(info.absoluteFilePath());
    if (!directory.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) {
        return false;
    }

    QDir parent = info.absoluteDir();
    if (!parent.rmdir(info.fileName())) {
        qWarning() << "Failed to remove empty temporary UUID directory for" << id << ":" << info.absoluteFilePath();
        return false;
    }

    qDebug() << "Removed empty temporary UUID directory:" << info.absoluteFilePath();
    return true;
}

int removeOrphanedUuidDirectories(const QString &root, const QSet<QString> &preservedIds)
{
    const QDir rootDir(QDir(root).absolutePath());
    if (!rootDir.exists()) {
        return 0;
    }

    int removedCount = 0;
    const QFileInfoList entries = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString id = entry.fileName();
        if (!isUuidDirectoryName(id) || preservedIds.contains(id)) {
            continue;
        }
        if (entry.isSymLink()) {
            qWarning() << "Skipping symlink in temporary root during orphan sweep:" << entry.absoluteFilePath();
            continue;
        }

        QDir orphan(entry.absoluteFilePath());
        if (orphan.removeRecursively()) {
            ++removedCount;
            qInfo() << "Removed orphaned download temporary directory:" << entry.absoluteFilePath();
        } else {
            qWarning() << "Failed to remove orphaned download temporary directory:" << entry.absoluteFilePath();
        }
    }
    return removedCount;
}

} // namespace DownloadTempCleanup
