#pragma once

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringView>
#include <QTimer>
#include <QUrl>

namespace YtDlpWorkerProcessHelpers {

[[nodiscard]] inline QString redactUrlSecrets(const QString &text)
{
    static const QRegularExpression urlPattern(QStringLiteral(R"(https?://[^\s\"'<>]+)"),
                                               QRegularExpression::CaseInsensitiveOption);
    QString redacted;
    qsizetype cursor = 0;
    auto matches = urlPattern.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        redacted.append(text.mid(cursor, match.capturedStart() - cursor));
        QUrl url(match.captured());
        if (url.isValid() && !url.host().isEmpty()) {
            url.setUserName(QString());
            url.setPassword(QString());
            if (url.hasQuery()) {
                url.setQuery(QStringLiteral("REDACTED"));
            }
            url.setFragment(QString());
            redacted.append(url.toString(QUrl::FullyEncoded));
        } else {
            redacted.append(QStringLiteral("<redacted-url>"));
        }
        cursor = match.capturedEnd();
    }
    redacted.append(text.mid(cursor));
    return redacted;
}

inline bool isWaitThumbnail(const QString &thumbnailPath, const QString &id)
{
    if (thumbnailPath.isEmpty()) {
        return false;
    }
    const QStringView view(thumbnailPath);
    const qsizetype slashIndex = qMax(view.lastIndexOf(QLatin1Char('/')), view.lastIndexOf(QLatin1Char('\\')));
    const QStringView fileName = slashIndex != -1 ? view.mid(slashIndex + 1) : view;
    return fileName.startsWith(id)
        && fileName.mid(id.length()).startsWith(QStringLiteral("_wait_thumbnail"));
}

inline void safeRemoveFile(const QString &filePath, const QString &description, int retries = 3)
{
    if (filePath.isEmpty()) {
        return;
    }
    if (QFile::remove(filePath)) {
        qDebug() << "Cleaned up" << description << "file:" << filePath;
    } else if (QFile::exists(filePath)) {
        qWarning() << "Failed to clean up" << description << "file:" << filePath;
        if (retries > 0) {
            QTimer::singleShot(100, [filePath, description, retries]() {
                safeRemoveFile(filePath, description, retries - 1);
            });
        } else {
            qWarning() << "Failed to clean up" << description << "file:" << filePath
                       << "after bounded retries.";
        }
    }
}

inline void cleanupWaitThumbnail(QString &thumbnailPath, const QString &id)
{
    if (isWaitThumbnail(thumbnailPath, id)) {
        safeRemoveFile(thumbnailPath, QStringLiteral("orphaned wait thumbnail"));
        thumbnailPath.clear();
    }
}

[[nodiscard]] inline QJsonObject parseJsonData(const QByteArray &jsonData, QString *errorString = nullptr)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        return document.object();
    }
    if (errorString) {
        *errorString = parseError.errorString();
    }
    return {};
}

} // namespace YtDlpWorkerProcessHelpers
