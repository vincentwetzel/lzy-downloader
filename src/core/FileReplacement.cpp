#include "FileReplacement.h"

#include <QDebug>
#include <QFile>
#include <QThread>
#include <QUuid>

namespace {

bool removeWithRetry(const QString &path, int attempts = 5)
{
    if (!QFile::exists(path)) {
        return true;
    }
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (QFile::remove(path)) {
            return true;
        }
    }
    return false;
}

bool renameWithRetry(const QString &sourcePath, const QString &destinationPath, int attempts = 5)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (QFile::rename(sourcePath, destinationPath)) {
            return true;
        }
        QThread::msleep(100);
    }
    return false;
}

bool moveOrCopy(const QString &sourcePath, const QString &destinationPath)
{
    if (renameWithRetry(sourcePath, destinationPath)) {
        return true;
    }
    if (!QFile::copy(sourcePath, destinationPath)) {
        return false;
    }
    if (!removeWithRetry(sourcePath)) {
        qWarning() << "Replacement copied successfully but could not remove the source file:" << sourcePath;
    }
    return true;
}

}

namespace FileReplacement {

bool moveReplacing(const QString &sourcePath, const QString &destinationPath)
{
    if (sourcePath.isEmpty() || destinationPath.isEmpty() || !QFile::exists(sourcePath)) {
        return false;
    }
    if (sourcePath == destinationPath) {
        return true;
    }

    if (!QFile::exists(destinationPath)) {
        return moveOrCopy(sourcePath, destinationPath);
    }

    const QString backupPath = destinationPath
        + QStringLiteral(".lzy-replacement-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QStringLiteral(".bak");
    if (!renameWithRetry(destinationPath, backupPath)) {
        qWarning() << "Could not reserve existing destination for replacement:" << destinationPath;
        return false;
    }

    if (moveOrCopy(sourcePath, destinationPath)) {
        if (!removeWithRetry(backupPath)) {
            qWarning() << "Replacement succeeded but old destination backup could not be removed:" << backupPath;
        }
        return true;
    }

    // A failed copy can leave a partial destination behind. Remove only that
    // new partial file, then restore the original destination.
    if (!removeWithRetry(destinationPath)) {
        qWarning() << "Could not remove failed replacement output:" << destinationPath;
        return false;
    }
    if (!renameWithRetry(backupPath, destinationPath)) {
        qWarning() << "Could not restore original destination after replacement failure:" << destinationPath;
        return false;
    }
    return false;
}

}
