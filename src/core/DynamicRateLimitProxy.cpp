#include "DynamicRateLimitProxy.h"

#include <QDebug>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

class ProxyConnection final {
public:
    QTcpSocket *client = nullptr;
    QTcpSocket *upstream = nullptr;
    QByteArray requestBuffer;
    QByteArray upstreamPending;
    QByteArray downstreamPending;
    bool requestStarted = false;
    bool tunnelEstablished = false;
    bool upstreamClosed = false;
    bool responseBytesSent = false;
};

namespace {
constexpr qint64 SOCKET_BUFFER_LIMIT = 64 * 1024;
constexpr qint64 MAX_PUMP_CHUNK = 16 * 1024;
constexpr int PUMP_INTERVAL_MS = 10;

qint64 parseRatePreset(const QString &preset)
{
    const QString normalized = preset.trimmed();
    if (normalized.compare(QLatin1String("Unlimited"), Qt::CaseInsensitive) == 0) {
        return -1;
    }

    QString digits = normalized;
    qint64 multiplier = 1;
    if (digits.endsWith(QLatin1String(" MB/s"), Qt::CaseInsensitive)) {
        multiplier = 1024 * 1024;
        digits.chop(5);
    } else if (digits.endsWith(QLatin1String(" KB/s"), Qt::CaseInsensitive)) {
        multiplier = 1024;
        digits.chop(5);
    } else {
        return -1;
    }

    bool ok = false;
    const double amount = digits.trimmed().toDouble(&ok);
    if (!ok || amount <= 0.0 || amount * static_cast<double>(multiplier) > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return -1;
    }
    return qMax<qint64>(1, static_cast<qint64>(amount * static_cast<double>(multiplier)));
}

void writeClientError(QTcpSocket *socket, int code, const QByteArray &reason)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    const QByteArray body = reason + '\n';
    socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(code) + ' ' + reason + "\r\nConnection: close\r\nContent-Length: "
                  + QByteArray::number(body.size()) + "\r\n\r\n" + body);
    socket->disconnectFromHost();
}
}

DynamicRateLimitProxy::DynamicRateLimitProxy(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_pumpTimer(new QTimer(this))
{
    m_clock.start();
    m_pumpTimer->setInterval(PUMP_INTERVAL_MS);
    connect(m_server, &QTcpServer::newConnection, this, &DynamicRateLimitProxy::acceptConnections);
    connect(m_pumpTimer, &QTimer::timeout, this, &DynamicRateLimitProxy::pumpConnections);
}

DynamicRateLimitProxy::~DynamicRateLimitProxy()
{
    stop();
}

bool DynamicRateLimitProxy::start()
{
    if (m_server->isListening()) {
        return true;
    }
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        qWarning("DynamicRateLimitProxy could not listen on localhost: %s", qPrintable(m_server->errorString()));
        return false;
    }
    m_lastPumpMs = m_clock.elapsed();
    m_lastSpeedReportMs = m_lastPumpMs;
    m_pumpTimer->start();
    return true;
}

void DynamicRateLimitProxy::stop()
{
    m_pumpTimer->stop();
    m_server->close();
    const auto connections = m_connections;
    for (ProxyConnection *connection : connections) {
        closeConnection(connection);
    }
}

QString DynamicRateLimitProxy::proxyUrl() const
{
    if (!m_server->isListening()) {
        return QString();
    }
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort());
}

void DynamicRateLimitProxy::setRateLimitPreset(const QString &preset)
{
    setRateLimitBytesPerSecond(parseRatePreset(preset));
}

void DynamicRateLimitProxy::setRateLimitBytesPerSecond(qint64 bytesPerSecond)
{
    m_rateLimitBytesPerSecond = bytesPerSecond > 0 ? bytesPerSecond : -1;
    m_availableBytes = 0.0;
    m_lastPumpMs = m_clock.elapsed();
}

void DynamicRateLimitProxy::acceptConnections()
{
    while (m_server->hasPendingConnections()) {
        auto *connection = new ProxyConnection;
        connection->client = m_server->nextPendingConnection();
        connection->client->setReadBufferSize(SOCKET_BUFFER_LIMIT);
        m_connections.append(connection);

        connect(connection->client, &QTcpSocket::readyRead, this, [this, connection]() {
            if (!m_connections.contains(connection)) {
                return;
            }
            const QByteArray bytes = connection->client->readAll();
            if (!connection->requestStarted) {
                connection->requestBuffer += bytes;
                if (connection->requestBuffer.size() > SOCKET_BUFFER_LIMIT) {
                    writeClientError(connection->client, 431, QByteArrayLiteral("Request Header Too Large"));
                    closeConnection(connection);
                    return;
                }
                if (connection->requestBuffer.contains(QByteArrayLiteral("\r\n\r\n"))) {
                    beginRequest(connection);
                }
            } else if (connection->upstream && !bytes.isEmpty()) {
                connection->upstream->write(bytes);
            }
        });
        connect(connection->client, &QTcpSocket::disconnected, this, [this, connection]() {
            closeConnection(connection);
        });
        connect(connection->client, &QTcpSocket::errorOccurred, this, [this, connection](QAbstractSocket::SocketError) {
            closeConnection(connection);
        });
    }
}

void DynamicRateLimitProxy::beginRequest(ProxyConnection *connection)
{
    const qsizetype headerEnd = connection->requestBuffer.indexOf(QByteArrayLiteral("\r\n\r\n"));
    if (headerEnd < 0) {
        return;
    }
    const QByteArray header = connection->requestBuffer.left(headerEnd);
    const QByteArray trailing = connection->requestBuffer.mid(headerEnd + 4);
    connection->requestBuffer.clear();

    const QList<QByteArray> lines = header.split('\n');
    if (lines.isEmpty()) {
        writeClientError(connection->client, 400, QByteArrayLiteral("Bad Request"));
        closeConnection(connection);
        return;
    }
    const QByteArray requestLine = lines.first().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() != 3) {
        writeClientError(connection->client, 400, QByteArrayLiteral("Bad Request"));
        closeConnection(connection);
        return;
    }

    connection->upstream = new QTcpSocket(this);
    connection->upstream->setReadBufferSize(SOCKET_BUFFER_LIMIT);
    connection->requestStarted = true;
    connect(connection->upstream, &QTcpSocket::readyRead, this, [this, connection]() { pumpConnection(connection); });
    connect(connection->upstream, &QTcpSocket::bytesWritten, this, [this, connection](qint64) { pumpConnection(connection); });
    connect(connection->upstream, &QTcpSocket::disconnected, this, [this, connection]() {
        connection->upstreamClosed = true;
        pumpConnection(connection);
    });
    connect(connection->upstream, &QTcpSocket::errorOccurred, this, [this, connection](QAbstractSocket::SocketError) {
        if (!connection->tunnelEstablished && !connection->responseBytesSent
            && connection->downstreamPending.isEmpty() && connection->upstream->bytesAvailable() == 0
            && connection->client && connection->client->state() == QAbstractSocket::ConnectedState) {
            writeClientError(connection->client, 502, QByteArrayLiteral("Bad Gateway"));
            closeConnection(connection);
            return;
        }
        connection->upstreamClosed = true;
        pumpConnection(connection);
    });

    if (parts.at(0).compare(QByteArrayLiteral("CONNECT"), Qt::CaseInsensitive) == 0) {
        const QUrl target(QStringLiteral("http://%1/").arg(QString::fromUtf8(parts.at(1))));
        const int port = target.port(443);
        if (!target.isValid() || target.host().isEmpty() || port <= 0) {
            writeClientError(connection->client, 400, QByteArrayLiteral("Bad CONNECT Target"));
            closeConnection(connection);
            return;
        }
        connection->upstreamPending = trailing;
        connect(connection->upstream, &QTcpSocket::connected, this, [connection]() {
            connection->tunnelEstablished = true;
            connection->client->write(QByteArrayLiteral("HTTP/1.1 200 Connection Established\r\n\r\n"));
            if (!connection->upstreamPending.isEmpty()) {
                connection->upstream->write(connection->upstreamPending);
                connection->upstreamPending.clear();
            }
        });
        connection->upstream->connectToHost(target.host(), static_cast<quint16>(port));
        return;
    }

    const QUrl target = QUrl::fromEncoded(parts.at(1));
    if (!target.isValid() || target.scheme() != QLatin1String("http") || target.host().isEmpty()) {
        writeClientError(connection->client, 400, QByteArrayLiteral("Unsupported Proxy Request"));
        closeConnection(connection);
        return;
    }
    const int port = target.port(80);
    if (port <= 0) {
        writeClientError(connection->client, 400, QByteArrayLiteral("Bad Proxy Port"));
        closeConnection(connection);
        return;
    }
    QByteArray path = target.path(QUrl::FullyEncoded).toUtf8();
    if (path.isEmpty()) {
        path = QByteArrayLiteral("/");
    }
    if (target.hasQuery()) {
        path += '?' + target.query(QUrl::FullyEncoded).toUtf8();
    }
    QByteArray forward = parts.at(0) + ' ' + path + ' ' + parts.at(2) + "\r\n";
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        if (line.isEmpty() || line.toLower().startsWith(QByteArrayLiteral("proxy-authorization:"))
            || line.toLower().startsWith(QByteArrayLiteral("connection:"))) {
            continue;
        }
        forward += line + "\r\n";
    }
    forward += QByteArrayLiteral("Connection: close\r\n\r\n");
    forward += trailing;
    connection->upstreamPending = forward;
    connect(connection->upstream, &QTcpSocket::connected, this, [connection]() {
        connection->upstream->write(connection->upstreamPending);
        connection->upstreamPending.clear();
    });
    connection->upstream->connectToHost(target.host(), static_cast<quint16>(port));
}

void DynamicRateLimitProxy::closeConnection(ProxyConnection *connection)
{
    if (!connection || !m_connections.removeOne(connection)) {
        return;
    }
    if (connection->client) {
        connection->client->disconnect(this);
        if (connection->client->state() != QAbstractSocket::UnconnectedState) {
            connection->client->abort();
        }
        connection->client->deleteLater();
    }
    if (connection->upstream) {
        connection->upstream->disconnect(this);
        if (connection->upstream->state() != QAbstractSocket::UnconnectedState) {
            connection->upstream->abort();
        }
        connection->upstream->deleteLater();
    }
    delete connection;
}

void DynamicRateLimitProxy::pumpConnections()
{
    const qint64 now = m_clock.elapsed();
    const qint64 elapsed = qMax<qint64>(0, now - m_lastPumpMs);
    m_lastPumpMs = now;
    if (m_rateLimitBytesPerSecond > 0) {
        // Bound the burst to 100 ms of allowance while retaining scheduler jitter.
        const double burst = qMax(1.0, static_cast<double>(m_rateLimitBytesPerSecond) * 0.1);
        m_availableBytes = qMin(burst, m_availableBytes + static_cast<double>(m_rateLimitBytesPerSecond) * elapsed / 1000.0);
    } else {
        m_availableBytes = std::numeric_limits<double>::max();
    }

    if (!m_connections.isEmpty()) {
        const int count = m_connections.size();
        for (int offset = 0; offset < count; ++offset) {
            const int index = (m_roundRobinStart + offset) % count;
            if (index < m_connections.size()) {
                pumpConnection(m_connections.at(index));
            }
        }
        m_roundRobinStart = (m_roundRobinStart + 1) % qMax(1, m_connections.size());
    }

    const qint64 speedElapsed = now - m_lastSpeedReportMs;
    if (speedElapsed >= 250) {
        emit totalDownloadSpeedUpdated(static_cast<double>(m_bytesSentSinceSpeedReport) * 1000.0 / speedElapsed);
        m_bytesSentSinceSpeedReport = 0;
        m_lastSpeedReportMs = now;
    }
}

void DynamicRateLimitProxy::pumpConnection(ProxyConnection *connection)
{
    if (!connection || !m_connections.contains(connection) || !connection->upstream || !connection->client) {
        return;
    }
    if (connection->client->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const qint64 pendingCapacity = SOCKET_BUFFER_LIMIT - connection->downstreamPending.size();
    if (pendingCapacity > 0 && connection->upstream->bytesAvailable() > 0) {
        connection->downstreamPending += connection->upstream->read(qMin(pendingCapacity, connection->upstream->bytesAvailable()));
    }
    const auto disconnectIfDrained = [connection]() {
        if (connection->upstreamClosed && connection->downstreamPending.isEmpty()
            && connection->upstream->bytesAvailable() == 0 && connection->client->bytesToWrite() == 0) {
            connection->client->disconnectFromHost();
        }
    };

    const qint64 clientCapacity = SOCKET_BUFFER_LIMIT - connection->client->bytesToWrite();
    if (clientCapacity <= 0 || connection->downstreamPending.isEmpty()) {
        disconnectIfDrained();
        return;
    }
    const qint64 allowed = m_rateLimitBytesPerSecond > 0
        ? qMin<qint64>(static_cast<qint64>(m_availableBytes), MAX_PUMP_CHUNK)
        : MAX_PUMP_CHUNK;
    const qint64 sendCount = std::min({allowed, clientCapacity, static_cast<qint64>(connection->downstreamPending.size())});
    if (sendCount <= 0) {
        disconnectIfDrained();
        return;
    }
    const QByteArray chunk = connection->downstreamPending.left(static_cast<qsizetype>(sendCount));
    connection->downstreamPending.remove(0, static_cast<qsizetype>(sendCount));
    connection->client->write(chunk);
    connection->responseBytesSent = true;
    m_bytesSentSinceSpeedReport += sendCount;
    if (m_rateLimitBytesPerSecond > 0) {
        m_availableBytes -= sendCount;
    }
    disconnectIfDrained();
}
