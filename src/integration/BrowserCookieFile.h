#pragma once

#include <QJsonArray>
#include <QString>
#include <QUrl>

namespace BrowserCookieFile {

struct CreateResult {
    bool success = false;
    QString path;
    QString error;
};

CreateResult createForUrl(const QJsonArray &cookies, const QUrl &url);
bool isOwnedPath(const QString &path);
void cleanupExpired();
void remove(const QString &path);

} // namespace BrowserCookieFile
