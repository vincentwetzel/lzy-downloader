#include "TestLocalApiServer.h"
#include "utils/UrlUtils.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QSignalSpy>
#include <QTimer>
#include <QEventLoop>
#include <QScopeGuard>
#include <QTcpServer>
#include <QDir>
#include <QFile>

QUrl TestLocalApiServer::apiUrl(const QString &path) const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_apiServer->port()).arg(path));
}

void TestLocalApiServer::init() {
    BaseTest::init();
    m_apiServer = new LocalApiServer(getConfigManager(), this);
    m_apiServer->start();
    if (!m_apiServer->isRunning()) {
        QSKIP("The configured Local API port is already in use. Skipping test.");
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

void TestLocalApiServer::testConfiguredPort() {
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 requestedPort = probe.serverPort();
    probe.close();

    QVERIFY(getConfigManager()->set(QStringLiteral("General"), QStringLiteral("local_api_port"), requestedPort));
    QTRY_VERIFY_WITH_TIMEOUT(m_apiServer->isRunning(), 3000);
    QCOMPARE(m_apiServer->port(), requestedPort);

    QFile portFile(QDir(getConfigManager()->getConfigDir()).filePath(QStringLiteral("api_port.txt")));
    QVERIFY(portFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(portFile.readAll()).trimmed(), QString::number(requestedPort));
}

void TestLocalApiServer::testUnauthorizedAccess() {
    QNetworkAccessManager manager;
    QNetworkRequest request(apiUrl(QStringLiteral("/status")));
    
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

void TestLocalApiServer::testUrlUtilsExtractsSharedText() {
    QCOMPARE(
        UrlUtils::extractUrl(QStringLiteral(
            "Joe Trippi: Polling Update [https://example.test/label]"
            "(https://example.test/video?id=42).")),
        QStringLiteral("https://example.test/video?id=42"));
    QCOMPARE(
        UrlUtils::extractUrl(QStringLiteral("Shared: https://example.test/video).")),
        QStringLiteral("https://example.test/video"));
}

void TestLocalApiServer::testValidEnqueueRequest() {
    QSignalSpy spy(m_apiServer, &LocalApiServer::enqueueRequested);
    
    QNetworkAccessManager manager;
    QNetworkRequest request(apiUrl(QStringLiteral("/enqueue")));
    request.setRawHeader(QByteArrayLiteral("Authorization"), QStringLiteral("Bearer %1").arg(m_apiServer->getApiKey()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    
    QJsonObject json;
    json[QStringLiteral("url")] = QStringLiteral(
        "Joe Trippi: Polling Problem [https://www.youtube.com/watch?v=dQw4w9WgXcQ]"
        "(https://www.youtube.com/watch?v=dQw4w9WgXcQ).");
    json[QStringLiteral("type")] = QStringLiteral("video");
    json[QStringLiteral("override_archive")] = true;
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
    QVERIFY(args.at(3).toBool());
}

void TestLocalApiServer::testValidCancelRequest() {
    const QString jobId = QStringLiteral("cancel-test-job");
    QVariantMap itemData;
    itemData.insert(QStringLiteral("id"), jobId);
    itemData.insert(QStringLiteral("url"), QStringLiteral("https://example.com/media"));
    itemData.insert(QStringLiteral("status"), QStringLiteral("Queued"));
    m_apiServer->onDownloadAdded(itemData);
    QSignalSpy spy(m_apiServer, &LocalApiServer::cancelRequested);

    QNetworkAccessManager manager;
    QNetworkRequest request(apiUrl(QStringLiteral("/cancel")));
    request.setRawHeader(QByteArrayLiteral("Authorization"), QStringLiteral("Bearer %1").arg(m_apiServer->getApiKey()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject json;
    json[QStringLiteral("job_id")] = jobId;
    QNetworkReply *reply = manager.post(request, QJsonDocument(json).toJson(QJsonDocument::Compact));
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
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), jobId);
}

void TestLocalApiServer::testClientScopedStatusAndCancellation() {
    const QString firstJobId = QStringLiteral("scope-job-one");
    const QString secondJobId = QStringLiteral("scope-job-two");
    const QString firstClientId = QStringLiteral("browser-client-one");
    const QString secondClientId = QStringLiteral("browser-client-two");
    QNetworkAccessManager manager;

    auto enqueue = [&](const QString &jobId, const QString &clientId) {
        QNetworkRequest request(apiUrl(QStringLiteral("/enqueue")));
        request.setRawHeader(QByteArrayLiteral("Authorization"), QStringLiteral("Bearer %1").arg(m_apiServer->getApiKey()).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QJsonObject json;
        json[QStringLiteral("url")] = QStringLiteral("https://example.com/%1").arg(jobId);
        json[QStringLiteral("id")] = jobId;
        json[QStringLiteral("client_id")] = clientId;
        QNetworkReply *reply = manager.post(request, QJsonDocument(json).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        QCOMPARE(status, 200);

        QVariantMap itemData;
        itemData.insert(QStringLiteral("id"), jobId);
        itemData.insert(QStringLiteral("status"), QStringLiteral("Queued"));
        itemData.insert(QStringLiteral("progress"), 0);
        m_apiServer->onDownloadAdded(itemData);
    };

    enqueue(firstJobId, firstClientId);
    enqueue(secondJobId, secondClientId);

    QUrl statusUrl = apiUrl(QStringLiteral("/status"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), firstClientId);
    statusUrl.setQuery(query);
    QNetworkRequest statusRequest(statusUrl);
    statusRequest.setRawHeader(QByteArrayLiteral("Authorization"), QStringLiteral("Bearer %1").arg(m_apiServer->getApiKey()).toUtf8());
    QNetworkReply *statusReply = manager.get(statusRequest);
    QEventLoop statusLoop;
    connect(statusReply, &QNetworkReply::finished, &statusLoop, &QEventLoop::quit);
    QTimer::singleShot(3000, &statusLoop, &QEventLoop::quit);
    statusLoop.exec();
    QCOMPARE(statusReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    const QJsonObject statusObject = QJsonDocument::fromJson(statusReply->readAll()).object();
    const QJsonArray jobs = statusObject.value(QStringLiteral("jobs")).toArray();
    QCOMPARE(jobs.size(), 1);
    QCOMPARE(jobs.first().toObject().value(QStringLiteral("id")).toString(), firstJobId);
    statusReply->deleteLater();

    QNetworkRequest wrongCancel(apiUrl(QStringLiteral("/cancel")));
    wrongCancel.setRawHeader(QByteArrayLiteral("Authorization"), QStringLiteral("Bearer %1").arg(m_apiServer->getApiKey()).toUtf8());
    wrongCancel.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject wrongBody;
    wrongBody[QStringLiteral("job_id")] = firstJobId;
    wrongBody[QStringLiteral("client_id")] = secondClientId;
    QNetworkReply *wrongReply = manager.post(wrongCancel, QJsonDocument(wrongBody).toJson(QJsonDocument::Compact));
    QEventLoop wrongLoop;
    connect(wrongReply, &QNetworkReply::finished, &wrongLoop, &QEventLoop::quit);
    QTimer::singleShot(3000, &wrongLoop, &QEventLoop::quit);
    wrongLoop.exec();
    QCOMPARE(wrongReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
    wrongReply->deleteLater();
}

QTEST_GUILESS_MAIN(TestLocalApiServer)
