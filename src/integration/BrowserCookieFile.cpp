#include "BrowserCookieFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtMath>
#include <QDateTime>
#include <QUuid>

namespace {

constexpr qsizetype kMaxCookies = 500;
constexpr qsizetype kMaxCookieFieldBytes = 4096;
constexpr qsizetype kMaxCookieFileBytes = 900 * 1024;
constexpr qint64 kMaxExpiration = 4102444800; // 2100-01-01 UTC

QString cookieDirectory()
{
    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return tempRoot.isEmpty() ? QString() : QDir(tempRoot).filePath(QStringLiteral("LzyDownloader/BrowserCookies"));
}

bool safeField(const QString &value)
{
    return value.toUtf8().size() <= kMaxCookieFieldBytes
        && !value.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
}

bool cookieAppliesToHost(const QString &domain, const QString &host)
{
    QString normalizedDomain = domain.toLower();
    if (normalizedDomain.startsWith(QLatin1Char('.'))) {
        normalizedDomain.remove(0, 1);
    }
    return !normalizedDomain.isEmpty()
        && (host == normalizedDomain || host.endsWith(QLatin1Char('.') + normalizedDomain));
}

bool cookieAppliesToPath(const QString &cookiePath, const QString &requestPath)
{
    if (!cookiePath.startsWith(QLatin1Char('/'))) {
        return false;
    }
    if (cookiePath == QLatin1String("/")) {
        return true;
    }
    if (!requestPath.startsWith(cookiePath)) {
        return false;
    }
    return cookiePath.endsWith(QLatin1Char('/')) || requestPath.size() == cookiePath.size()
        || requestPath.at(cookiePath.size()) == QLatin1Char('/');
}

} // namespace

namespace BrowserCookieFile {

CreateResult createForUrl(const QJsonArray &cookies, const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty() || cookies.isEmpty() || cookies.size() > kMaxCookies) {
        return {false, {}, QStringLiteral("No valid cookies were supplied for this URL.")};
    }

    QString content = QStringLiteral("# Netscape HTTP Cookie File\n");
    for (const QJsonValue &value : cookies) {
        if (!value.isObject()) {
            return {false, {}, QStringLiteral("The browser returned an invalid cookie entry.")};
        }

        const QJsonObject cookie = value.toObject();
        const QString name = cookie.value(QStringLiteral("name")).toString();
        const QString cookieValue = cookie.value(QStringLiteral("value")).toString();
        const QString domain = cookie.value(QStringLiteral("domain")).toString().toLower();
        const QString path = cookie.value(QStringLiteral("path")).toString();
        if (name.isEmpty() || domain.isEmpty() || path.isEmpty() || !safeField(name)
            || !safeField(cookieValue) || !safeField(domain) || !safeField(path)
            || !cookieAppliesToPath(path, url.path().isEmpty() ? QStringLiteral("/") : url.path())
            || !cookieAppliesToHost(domain, url.host().toLower())) {
            return {false, {}, QStringLiteral("A browser cookie was outside the requested URL scope.")};
        }

        const bool secure = cookie.value(QStringLiteral("secure")).toBool(false);
        if (secure && url.scheme() != QStringLiteral("https")) {
            return {false, {}, QStringLiteral("A secure browser cookie cannot be used with this URL.")};
        }

        qint64 expiration = 0;
        const QJsonValue expirationValue = cookie.value(QStringLiteral("expiration"));
        if (expirationValue.isDouble()) {
            const double numericExpiration = expirationValue.toDouble();
            if (numericExpiration < 0 || numericExpiration > kMaxExpiration
                || numericExpiration != qFloor(numericExpiration)) {
                return {false, {}, QStringLiteral("A browser cookie has an invalid expiration.")};
            }
            expiration = static_cast<qint64>(numericExpiration);
        }

        content += QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\n")
            .arg(domain, domain.startsWith(QLatin1Char('.')) ? QStringLiteral("TRUE") : QStringLiteral("FALSE"),
                 path, secure ? QStringLiteral("TRUE") : QStringLiteral("FALSE"),
                 QString::number(expiration), name, cookieValue);
        if (content.toUtf8().size() > kMaxCookieFileBytes) {
            return {false, {}, QStringLiteral("The browser cookie bundle is too large.")};
        }
    }

    const QString directory = cookieDirectory();
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        return {false, {}, QStringLiteral("LzyDownloader could not create its temporary cookie directory.")};
    }
    cleanupExpired();

    const QString path = QDir(directory).filePath(
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".txt"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(content.toUtf8()) != content.toUtf8().size() || !file.commit()) {
        return {false, {}, QStringLiteral("LzyDownloader could not create the temporary cookie file.")};
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return {true, path, {}};
}

bool isOwnedPath(const QString &path)
{
    const QString directory = cookieDirectory();
    if (directory.isEmpty()) {
        return false;
    }
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    const QString canonicalDirectory = QFileInfo(directory).canonicalFilePath();
    static const QRegularExpression nameRe(QStringLiteral("^[0-9a-f-]{36}\\.txt$"));
    return !canonicalPath.isEmpty() && !canonicalDirectory.isEmpty()
        && QFileInfo(canonicalPath).absolutePath() == canonicalDirectory
        && nameRe.match(QFileInfo(canonicalPath).fileName()).hasMatch();
}

void cleanupExpired()
{
    const QString directory = cookieDirectory();
    if (directory.isEmpty()) {
        return;
    }
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-24 * 60 * 60);
    const QDir cookieDir(directory);
    for (const QFileInfo &file : cookieDir.entryInfoList({QStringLiteral("*.txt")}, QDir::Files | QDir::NoSymLinks)) {
        if (file.lastModified().toUTC() < cutoff && isOwnedPath(file.absoluteFilePath())) {
            QFile::remove(file.absoluteFilePath());
        }
    }
}

void remove(const QString &path)
{
    if (isOwnedPath(path)) {
        QFile::remove(path);
    }
}

} // namespace BrowserCookieFile
