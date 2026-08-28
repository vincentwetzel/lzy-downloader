#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariantMap>
#include <QMap>
#include <QHash>
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
    void enqueueRequested(const QString &url, const QString &type, const QString &jobId, bool overrideArchive);
    void enqueueWithCookieFileRequested(const QString &url, const QString &type, const QString &jobId,
                                        bool overrideArchive, const QString &cookieFile);
    void cancelRequested(const QString &jobId);

public slots:
    void onDownloadAdded(const QVariantMap &itemData);
    void onDownloadProgress(const QString &id, const QVariantMap &progressData);
    void onDownloadFinished(const QString &id, bool success, const QString &message);
    void onDownloadCancelled(const QString &id);
    void onDownloadRemoved(const QString &id);
    void onNonInteractiveRequestFailed(const QString &id, const QString &url, const QString &error);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    ConfigManager *m_configManager;
    QTcpServer *m_server;
    QString m_apiKey;
    QMap<QString, QVariantMap> m_activeJobs;
    QHash<QString, QString> m_jobClients;
    QHash<QString, QString> m_jobCookieFiles;

    void generateOrLoadApiKey();
    void removeOwnedCookieFile(const QString &jobId);
    void handleRequest(QTcpSocket *socket, const QByteArray &requestData);
    void sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText, const QByteArray &body);
};
