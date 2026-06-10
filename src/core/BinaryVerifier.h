#pragma once

#include <QString>
#include <QFile>
#include <QCryptographicHash>
#include <QDebug>

namespace BinaryVerifier {

/**
 * @brief Validates that a downloaded binary file matches an expected SHA-256 checksum string.
 */
inline bool verifyBinaryIntegrity(const QString& filePath, const QString& expectedSha256) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open binary file for validation check:" << filePath;
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (hash.addData(&file)) {
        const QString calculatedHash = hash.result().toHex().toLower();
        const QString cleanedExpected = expectedSha256.trimmed().toLower();
        
        if (calculatedHash == cleanedExpected) {
            qInfo() << "Verification success for binary file:" << filePath;
            return true;
        }
        qCritical() << "Verification FAILED. Expected:" << cleanedExpected << "Calculated:" << calculatedHash;
    }
    
    return false;
}

} // namespace BinaryVerifier