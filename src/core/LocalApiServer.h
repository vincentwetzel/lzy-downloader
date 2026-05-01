#ifndef LOCALAPISERVER_H
#define LOCALAPISERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariantMap>
#include <QMap>
#include "core/ConfigManager.h"

class LocalApiServer : public QObject {
    Q_OBJECT
public:
    explicit LocalApiServer(ConfigManager *configManager, QObject *parent = nullptr);
    ~LocalApiServer();

    void start();
    void stop();

    bool isRunning() const;
    QString getApiKey() const;

signals:
    void enqueueRequested(const QString &url, const QString &type);

public slots:
    void onDownloadAdded(const QVariantMap &itemData);
    void onDownloadProgress(const QString &id, const QVariantMap &progressData);
    void onDownloadFinished(const QString &id, bool success, const QString &message);
    void onDownloadRemoved(const QString &id);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    ConfigManager *m_configManager;
    QTcpServer *m_server;
    QString m_apiKey;
    QMap<QString, QVariantMap> m_activeJobs;

    void generateOrLoadApiKey();
    void handleRequest(QTcpSocket *socket, const QByteArray &requestData);
    void sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText, const QByteArray &body);
};

#endif // LOCALAPISERVER_H