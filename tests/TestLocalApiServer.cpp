#include "TestLocalApiServer.h"
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
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:8765/cancel")));
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
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:8765/enqueue")));
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

    QUrl statusUrl(QStringLiteral("http://127.0.0.1:8765/status"));
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

    QNetworkRequest wrongCancel(QUrl(QStringLiteral("http://127.0.0.1:8765/cancel")));
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
