#pragma once

#include <QString>
#include <QMap>
#include <QStringList>

class BinaryFinder {
public:
    /**
     * @brief Finds the absolute path to a specific binary.
     * @param binaryName The name of the executable (e.g., "yt-dlp").
     * @return The absolute path to the executable, or an empty string if not found.
     */
    static QString findBinary(const QString& binaryName);

    /**
     * @brief Finds all required dependencies for the application.
     * @return A map where keys are binary names and values are their resolved absolute paths.
     */
    static QMap<QString, QString> findAllBinaries();

private:
    static QStringList getExtendedSearchPaths();
};