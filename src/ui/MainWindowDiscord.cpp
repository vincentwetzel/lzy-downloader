#include "MainWindow.h"

#include "core/DownloadManager.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSharedPointer>
#include <QUrl>

void MainWindow::connectDiscordWebhookSignals()
{
    QNetworkAccessManager *discordNetworkManager = new QNetworkAccessManager(this);

    auto sendDiscordWebhook = [discordNetworkManager](const QString &jobId, const QVariantMap &state) {
        QJsonObject json;
        json["job_id"] = jobId;

        if (state.contains("parent_id") && !state.value("parent_id").toString().isEmpty()) {
            json["parent_id"] = state.value("parent_id").toString();
        }

        json["url"] = state.value("url").toString();
        if (state.contains("title") && !state.value("title").toString().isEmpty()) {
            json["title"] = state.value("title").toString();
        }
        json["download_type"] = state.value("download_type", "video").toString();
        
        QString currentStatus = state.value("status").toString();
        currentStatus.replace('\n', ' ').replace('\r', "");
        
        if (currentStatus.length() > 200) {
            currentStatus = currentStatus.left(197) + "...";
        }
        
        json["status"] = currentStatus;
        json["progress"] = state.value("progress").toDouble();
        json["speed"] = state.value("speed").toString();
        json["eta"] = state.value("eta").toString();

        QJsonDocument doc(json);
        QByteArray payload = doc.toJson(QJsonDocument::Compact);

        qDebug() << "Sending webhook update for job:" << jobId << "Progress:" << json["progress"].toDouble();

        QNetworkRequest request(QUrl("http://127.0.0.1:8766/webhook"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *reply = discordNetworkManager->post(request, payload);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, jobId]() {
            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "Discord Webhook error for job:" << jobId << reply->errorString() << reply->readAll();
            } else {
                int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qDebug() << "Discord Webhook success for job:" << jobId << "HTTP Status:" << statusCode;
            }
            reply->deleteLater();
        });
    };

    QSharedPointer<QMap<QString, QVariantMap>> webhookStates = QSharedPointer<QMap<QString, QVariantMap>>::create();
    QSharedPointer<QMap<QString, qint64>> webhookTimestamps = QSharedPointer<QMap<QString, qint64>>::create();

    connect(m_downloadManager, &DownloadManager::downloadAddedToQueue, this, [webhookStates](const QVariantMap &itemData) {
        QString id = itemData.value("id").toString();
        QVariantMap state;
        state["url"] = itemData.value("url");
        state["title"] = itemData.value("title");
        QVariantMap options = itemData.value("options").toMap();
        state["download_type"] = options.value("type", "video");
        state["status"] = itemData.value("status", "Queued");
        
        if (webhookStates->contains(id) && !itemData.contains("progress")) {
            state["progress"] = (*webhookStates)[id].value("progress", 0.0).toDouble();
        } else {
            state["progress"] = itemData.value("progress", 0.0).toDouble();
        }

        if (options.contains("playlist_placeholder_id")) {
            state["parent_id"] = options.value("playlist_placeholder_id").toString();
        }

        (*webhookStates)[id] = state;
    });

    connect(m_downloadManager, &DownloadManager::downloadProgress, this, [sendDiscordWebhook, webhookStates, webhookTimestamps](const QString &id, const QVariantMap &data) {
        if (!webhookStates->contains(id)) {
            if (data.contains("url") && data.contains("options")) {
                QVariantMap state;
                state["url"] = data.value("url");
                state["title"] = data.value("title");
                state["download_type"] = data.value("options").toMap().value("type", "video").toString();
                state["status"] = data.value("status", "Queued").toString();
                
                if (webhookStates->contains(id) && !data.contains("progress")) {
                    state["progress"] = (*webhookStates)[id].value("progress", 0.0).toDouble();
                } else {
                    state["progress"] = data.value("progress", 0.0).toDouble();
                }

                QVariantMap options = data.value("options").toMap();
                if (options.contains("playlist_placeholder_id")) {
                    state["parent_id"] = options.value("playlist_placeholder_id").toString();
                }

                (*webhookStates)[id] = state;
            } else {
                return;
            }
        }

        QVariantMap &state = (*webhookStates)[id];
        QString oldStatus = state.value("status").toString();

        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            state[it.key()] = it.value();
        }

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 lastSent = webhookTimestamps->value(id, 0);

        bool shouldSend = false;
        if (now - lastSent >= 1500) {
            shouldSend = true;
        } else if (data.contains("status") && state.value("status").toString() != oldStatus) {
            shouldSend = true;
        }

        if (shouldSend) {
            (*webhookTimestamps)[id] = now;
            sendDiscordWebhook(id, state);
        }
    });

    connect(m_downloadManager, &DownloadManager::downloadFinished, this, [sendDiscordWebhook, webhookStates, webhookTimestamps](const QString &id) {
        if (!webhookStates->contains(id)) return;
        QVariantMap state = (*webhookStates)[id];
        state["status"] = "Completed";
        state["progress"] = 100.0;
        sendDiscordWebhook(id, state);
    });

    connect(m_downloadManager, &DownloadManager::downloadCancelled, this, [sendDiscordWebhook, webhookStates, webhookTimestamps](const QString &id) {
        if (!webhookStates->contains(id)) return;
        QVariantMap state = (*webhookStates)[id];
        state["status"] = "Cancelled";
        sendDiscordWebhook(id, state);
    });

    connect(m_downloadManager, &DownloadManager::downloadPaused, this, [sendDiscordWebhook, webhookStates](const QString &id) {
        if (!webhookStates->contains(id)) return;
        QVariantMap state = (*webhookStates)[id];
        state["status"] = "Paused";
        sendDiscordWebhook(id, state);
    });

    connect(m_downloadManager, &DownloadManager::downloadResumed, this, [sendDiscordWebhook, webhookStates](const QString &id) {
        if (!webhookStates->contains(id)) return;
        QVariantMap state = (*webhookStates)[id];
        state["status"] = "Resuming download...";
        sendDiscordWebhook(id, state);
    });

    connect(m_downloadManager, &DownloadManager::downloadRemovedFromQueue, this, [webhookStates, webhookTimestamps](const QString &id) {
        webhookStates->remove(id);
        webhookTimestamps->remove(id);
    });
}
