#include "TestLocalApiServer.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTimer>
#include <QEventLoop>
#include <QScopeGuard>

void TestLocalApiServer::init() {
    BaseTest::init();
    m_apiServer = new LocalApiServer(getConfigManager(), this);
    m_apiServer->start();
    if (!m_apiServer->isRunning()) {
        QSKIP("Port 8765 is already in use (is LzyDownloader already running?). Skipping test.");
    }
}

void TestLocalApiServer::cleanup() {
    if (m_apiServer) {
        m_apiServer->stop();
        m_apiServer->deleteLater();
        m_apiServer = nullptr;
    }
    BaseTest::cleanup();
}

void TestLocalApiServer::testStartupAndShutdown() {
    QVERIFY(m_apiServer->isRunning());
    m_apiServer->stop();
    QVERIFY(!m_apiServer->isRunning());
    
    // It should safely start back up
    m_apiServer->start();
    QVERIFY(m_apiServer->isRunning());
}

void TestLocalApiServer::testApiTokenGeneration() {
    QString token = m_apiServer->getApiKey();
    QVERIFY(!token.isEmpty());
    
    // Ensure the token remains consistent when reading from the generated file again
    LocalApiServer secondServer(getConfigManager(), nullptr);
    QCOMPARE(secondServer.getApiKey(), token);
}

void TestLocalApiServer::testUnauthorizedAccess() {
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:8765/status")));
    
    QNetworkReply *reply = manager.get(request);
    auto replyGuard = qScopeGuard([reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
        reply->deleteLater();
    });
    
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit); // 3 sec timeout guard
    loop.exec();
    
    QVERIFY2(!reply->isRunning(), "Network request timed out before finishing");
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 401);
}

void TestLocalApiServer::testValidEnqueueRequest() {
    QSignalSpy spy(m_apiServer, &LocalApiServer::enqueueRequested);
    
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:8765/enqueue")));
    request.setRawHeader(QByteArrayLiteral("Authorization"), QStringLiteral("Bearer %1").arg(m_apiServer->getApiKey()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    
    QJsonObject json;
    json[QStringLiteral("url")] = QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    json[QStringLiteral("type")] = QStringLiteral("video");
    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    
    QNetworkReply *reply = manager.post(request, data);
    auto replyGuard = qScopeGuard([reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
        reply->deleteLater();
    });
    
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    
    QVERIFY2(!reply->isRunning(), "Network request timed out before finishing");
    // Ensure the request was accepted
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    
    // Ensure the server successfully parsed the body and emitted the signal to DownloadManager
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("video"));
    QVERIFY(!args.at(2).toString().isEmpty()); // Job ID should be generated and not empty
}

QTEST_GUILESS_MAIN(TestLocalApiServer)