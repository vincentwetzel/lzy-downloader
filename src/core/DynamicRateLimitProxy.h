#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QList>
#include <QByteArray>
#include <QString>

class QTcpServer;
class QTimer;
class ProxyConnection;

/** Local HTTP proxy that applies one adjustable download cap across all jobs. */
class DynamicRateLimitProxy final : public QObject {
    Q_OBJECT

public:
    explicit DynamicRateLimitProxy(QObject *parent = nullptr);
    ~DynamicRateLimitProxy() override;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] QString proxyUrl() const;
    void setRateLimitPreset(const QString &preset);
    void setRateLimitBytesPerSecond(qint64 bytesPerSecond);
    [[nodiscard]] qint64 rateLimitBytesPerSecond() const noexcept { return m_rateLimitBytesPerSecond; }

signals:
    void totalDownloadSpeedUpdated(double bytesPerSecond);

private:
    void acceptConnections();
    void beginRequest(ProxyConnection *connection);
    void closeConnection(ProxyConnection *connection);
    void pumpConnections();
    void pumpConnection(ProxyConnection *connection);

    QTcpServer *m_server;
    QTimer *m_pumpTimer;
    QList<ProxyConnection *> m_connections;
    QElapsedTimer m_clock;
    qint64 m_rateLimitBytesPerSecond = -1;
    double m_availableBytes = 0.0;
    qint64 m_lastPumpMs = 0;
    qint64 m_lastSpeedReportMs = 0;
    qint64 m_bytesSentSinceSpeedReport = 0;
    int m_roundRobinStart = 0;
};
