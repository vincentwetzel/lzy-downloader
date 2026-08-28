#include "TestBrowserCookieFile.h"

#include "integration/BrowserCookieFile.h"

#include <QFile>
#include <QJsonObject>

void TestBrowserCookieFile::createsScopedNetscapeFile()
{
    QJsonObject cookie;
    cookie[QStringLiteral("name")] = QStringLiteral("session");
    cookie[QStringLiteral("value")] = QStringLiteral("synthetic-test-value");
    cookie[QStringLiteral("domain")] = QStringLiteral(".example.test");
    cookie[QStringLiteral("path")] = QStringLiteral("/");
    cookie[QStringLiteral("secure")] = true;
    cookie[QStringLiteral("expiration")] = 4102440000.0;

    const BrowserCookieFile::CreateResult result = BrowserCookieFile::createForUrl(
        QJsonArray{cookie}, QUrl(QStringLiteral("https://media.example.test/watch")));
    QVERIFY2(result.success, qPrintable(result.error));
    QVERIFY(QFile::exists(result.path));
    QFile file(result.path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray content = file.readAll();
    QVERIFY(content.startsWith("# Netscape HTTP Cookie File"));
    QVERIFY(content.contains("session\tsynthetic-test-value"));
    file.close();
    BrowserCookieFile::remove(result.path);
    QVERIFY(!QFile::exists(result.path));
}

void TestBrowserCookieFile::rejectsCookieOutsideUrlScope()
{
    QJsonObject cookie;
    cookie[QStringLiteral("name")] = QStringLiteral("session");
    cookie[QStringLiteral("value")] = QStringLiteral("synthetic-test-value");
    cookie[QStringLiteral("domain")] = QStringLiteral(".other.test");
    cookie[QStringLiteral("path")] = QStringLiteral("/");

    const BrowserCookieFile::CreateResult result = BrowserCookieFile::createForUrl(
        QJsonArray{cookie}, QUrl(QStringLiteral("https://media.example.test/watch")));
    QVERIFY(!result.success);
    QVERIFY(result.path.isEmpty());
}

void TestBrowserCookieFile::rejectsControlCharacters()
{
    QJsonObject cookie;
    cookie[QStringLiteral("name")] = QStringLiteral("session");
    cookie[QStringLiteral("value")] = QStringLiteral("bad\nvalue");
    cookie[QStringLiteral("domain")] = QStringLiteral("example.test");
    cookie[QStringLiteral("path")] = QStringLiteral("/");

    const BrowserCookieFile::CreateResult result = BrowserCookieFile::createForUrl(
        QJsonArray{cookie}, QUrl(QStringLiteral("https://example.test/watch")));
    QVERIFY(!result.success);
    QVERIFY(result.path.isEmpty());
}

QTEST_GUILESS_MAIN(TestBrowserCookieFile)
