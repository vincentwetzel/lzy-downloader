#include "core/DynamicRateLimitProxy.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QtTest/QtTest>

class TestDynamicRateLimitProxy final : public QObject {
    Q_OBJECT

private slots:
    void testRateCanChangeWhileTunnelStaysOpen();
    void testRateCanBeCappedWhileTunnelStaysOpen();
    void testRateLimitIsSharedAcrossTunnels();
};

void TestDynamicRateLimitProxy::testRateCanChangeWhileTunnelStaysOpen()
{
    QTcpServer origin;
    QVERIFY(origin.listen(QHostAddress::LocalHost, 0));
    const QByteArray payload(256 * 1024, 'x');
    connect(&origin, &QTcpServer::newConnection, &origin, [&origin, &payload]() {
        while (origin.hasPendingConnections()) {
            QTcpSocket *socket = origin.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            socket->write(payload);
        }
    });

    DynamicRateLimitProxy proxy;
    proxy.setRateLimitBytesPerSecond(1);
    QVERIFY(proxy.start());
    QSignalSpy speedSpy(&proxy, &DynamicRateLimitProxy::totalDownloadSpeedUpdated);

    QTcpSocket client;
    QByteArray received;
    connect(&client, &QTcpSocket::readyRead, &client, [&client, &received]() {
        received += client.readAll();
    });
    const QUrl proxyUrl(proxy.proxyUrl());
    client.connectToHost(QHostAddress::LocalHost, proxyUrl.port());
    QVERIFY(client.waitForConnected(2000));
    const QByteArray tunnelRequest = QByteArrayLiteral("CONNECT 127.0.0.1:")
        + QByteArray::number(origin.serverPort()) + QByteArrayLiteral(" HTTP/1.1\r\nHost: local-test\r\n\r\n");
    QCOMPARE(client.write(tunnelRequest), static_cast<qint64>(tunnelRequest.size()));

    QTRY_VERIFY_WITH_TIMEOUT(received.contains(QByteArrayLiteral("200 Connection Established\r\n\r\n")), 3000);
    const qsizetype headerEnd = received.indexOf(QByteArrayLiteral("\r\n\r\n")) + 4;
    received.remove(0, headerEnd);
    QTest::qWait(100);
    QVERIFY2(received.size() < payload.size(), "The low shared cap should hold back tunnel data.");

    // The same connected tunnel resumes at full speed after the live setting changes.
    proxy.setRateLimitBytesPerSecond(-1);
    QTRY_VERIFY_WITH_TIMEOUT(received.size() >= payload.size(), 5000);
    QVERIFY(client.state() == QAbstractSocket::ConnectedState);
    QCOMPARE(received.left(payload.size()), payload);
    QTRY_VERIFY_WITH_TIMEOUT(!speedSpy.isEmpty(), 1000);
    QVERIFY(speedSpy.last().at(0).toDouble() > 0.0);
}

void TestDynamicRateLimitProxy::testRateCanBeCappedWhileTunnelStaysOpen()
{
    QTcpServer origin;
    QVERIFY(origin.listen(QHostAddress::LocalHost, 0));
    QTcpSocket *originSocket = nullptr;
    const QByteArray payload(256 * 1024, 'y');
    const QByteArray prefix = payload.left(4096);
    connect(&origin, &QTcpServer::newConnection, &origin, [&origin, &originSocket, &prefix]() {
        originSocket = origin.nextPendingConnection();
        connect(originSocket, &QTcpSocket::disconnected, originSocket, &QObject::deleteLater);
        originSocket->write(prefix);
    });

    DynamicRateLimitProxy proxy;
    proxy.setRateLimitBytesPerSecond(-1);
    QVERIFY(proxy.start());

    QTcpSocket client;
    QByteArray received;
    connect(&client, &QTcpSocket::readyRead, &client, [&client, &received]() {
        received += client.readAll();
    });
    const QUrl proxyUrl(proxy.proxyUrl());
    client.connectToHost(QHostAddress::LocalHost, proxyUrl.port());
    QVERIFY(client.waitForConnected(2000));
    const QByteArray tunnelRequest = QByteArrayLiteral("CONNECT 127.0.0.1:")
        + QByteArray::number(origin.serverPort()) + QByteArrayLiteral(" HTTP/1.1\r\nHost: local-test\r\n\r\n");
    QCOMPARE(client.write(tunnelRequest), static_cast<qint64>(tunnelRequest.size()));

    QTRY_VERIFY_WITH_TIMEOUT(originSocket != nullptr, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains(QByteArrayLiteral("200 Connection Established\r\n\r\n")), 3000);
    const qsizetype headerEnd = received.indexOf(QByteArrayLiteral("\r\n\r\n")) + 4;
    received.remove(0, headerEnd);
    QTRY_COMPARE_WITH_TIMEOUT(received.size(), prefix.size(), 3000);

    // The connection is active and previously unlimited. Tightening the shared
    // cap must backpressure the existing tunnel without replacing it.
    proxy.setRateLimitBytesPerSecond(1024);
    const QByteArray remainder = payload.mid(prefix.size());
    QCOMPARE(originSocket->write(remainder), static_cast<qint64>(remainder.size()));
    QTest::qWait(100);
    QVERIFY2(received.size() < payload.size(), "The newly applied cap should hold back existing tunnel data.");

    proxy.setRateLimitBytesPerSecond(-1);
    QTRY_VERIFY_WITH_TIMEOUT(received.size() >= payload.size(), 5000);
    QVERIFY(client.state() == QAbstractSocket::ConnectedState);
    QCOMPARE(received, payload);
}

void TestDynamicRateLimitProxy::testRateLimitIsSharedAcrossTunnels()
{
    QTcpServer origin;
    QVERIFY(origin.listen(QHostAddress::LocalHost, 0));
    const QByteArray payload(512 * 1024, 'z');
    connect(&origin, &QTcpServer::newConnection, &origin, [&origin, &payload]() {
        while (origin.hasPendingConnections()) {
            QTcpSocket *socket = origin.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            socket->write(payload);
        }
    });

    DynamicRateLimitProxy proxy;
    constexpr qint64 sharedRateBytesPerSecond = 64 * 1024;
    proxy.setRateLimitBytesPerSecond(sharedRateBytesPerSecond);
    QVERIFY(proxy.start());
    QSignalSpy speedSpy(&proxy, &DynamicRateLimitProxy::totalDownloadSpeedUpdated);

    QTcpSocket firstClient;
    QTcpSocket secondClient;
    QByteArray firstResponse;
    QByteArray secondResponse;
    connect(&firstClient, &QTcpSocket::readyRead, &firstClient, [&]() { firstResponse += firstClient.readAll(); });
    connect(&secondClient, &QTcpSocket::readyRead, &secondClient, [&]() { secondResponse += secondClient.readAll(); });
    const QUrl proxyUrl(proxy.proxyUrl());
    firstClient.connectToHost(QHostAddress::LocalHost, proxyUrl.port());
    secondClient.connectToHost(QHostAddress::LocalHost, proxyUrl.port());
    QVERIFY(firstClient.waitForConnected(2000));
    QVERIFY(secondClient.waitForConnected(2000));

    const QByteArray firstRequest = QByteArrayLiteral("CONNECT 127.0.0.1:")
        + QByteArray::number(origin.serverPort()) + QByteArrayLiteral(" HTTP/1.1\r\nHost: first\r\n\r\n");
    QCOMPARE(firstClient.write(firstRequest), static_cast<qint64>(firstRequest.size()));
    const QByteArray secondRequest = QByteArrayLiteral("CONNECT 127.0.0.1:")
        + QByteArray::number(origin.serverPort()) + QByteArrayLiteral(" HTTP/1.1\r\nHost: second\r\n\r\n");
    QCOMPARE(secondClient.write(secondRequest), static_cast<qint64>(secondRequest.size()));

    QTRY_VERIFY_WITH_TIMEOUT(firstResponse.contains(QByteArrayLiteral("200 Connection Established\r\n\r\n")), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(secondResponse.contains(QByteArrayLiteral("200 Connection Established\r\n\r\n")), 3000);
    firstResponse.remove(0, firstResponse.indexOf(QByteArrayLiteral("\r\n\r\n")) + 4);
    secondResponse.remove(0, secondResponse.indexOf(QByteArrayLiteral("\r\n\r\n")) + 4);
    QTest::qWait(400);

    double maximumMeasuredSpeed = 0.0;
    for (const QList<QVariant> &sample : speedSpy) {
        maximumMeasuredSpeed = qMax(maximumMeasuredSpeed, sample.first().toDouble());
    }
    QVERIFY2(maximumMeasuredSpeed < sharedRateBytesPerSecond * 1.6,
             "Concurrent tunnels must share one aggregate rate allowance.");

    proxy.setRateLimitBytesPerSecond(-1);
    QTRY_VERIFY_WITH_TIMEOUT(firstResponse.size() >= payload.size(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(secondResponse.size() >= payload.size(), 5000);
    QCOMPARE(firstResponse.left(payload.size()), payload);
    QCOMPARE(secondResponse.left(payload.size()), payload);
}

QTEST_MAIN(TestDynamicRateLimitProxy)
#include "TestDynamicRateLimitProxy.moc"
