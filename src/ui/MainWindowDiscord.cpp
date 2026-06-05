#include "MainWindow.h"

#include "core/DownloadManager.h"
#include "ActiveDownloadsTab.h"

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
        json[QStringLiteral("job_id")] = jobId;

        if (state.contains(QStringLiteral("parent_id")) && !state.value(QStringLiteral("parent_id")).toString().isEmpty()) {
            json[QStringLiteral("parent_id")] = state.value(QStringLiteral("parent_id")).toString();
        }

        if (state.contains(QStringLiteral("queue_position")) && state.value(QStringLiteral("queue_position")).toInt() > 0) {
            json[QStringLiteral("queue_position")] = state.value(QStringLiteral("queue_position")).toInt();
        } else {
            json[QStringLiteral("queue_position")] = QJsonValue::Null;
        }

        json[QStringLiteral("url")] = state.value(QStringLiteral("url")).toString();
        if (state.contains(QStringLiteral("title")) && !state.value(QStringLiteral("title")).toString().isEmpty()) {
            json[QStringLiteral("title")] = state.value(QStringLiteral("title")).toString();
        }
        json[QStringLiteral("download_type")] = state.value(QStringLiteral("download_type"), QStringLiteral("video")).toString();

        QString currentStatus = state.value(QStringLiteral("status")).toString();
        currentStatus.replace(QLatin1Char('\n'), QLatin1Char(' ')).remove(QLatin1Char('\r'));
        
        if (currentStatus.length() > 200) {
            currentStatus = currentStatus.left(197) + QStringLiteral("...");
        }
        
        json[QStringLiteral("status")] = currentStatus;
        json[QStringLiteral("progress")] = state.value(QStringLiteral("progress")).toDouble();
        json[QStringLiteral("speed")] = state.value(QStringLiteral("speed")).toString();
        json[QStringLiteral("eta")] = state.value(QStringLiteral("eta")).toString();

        QJsonDocument doc(json);
        QByteArray payload = doc.toJson(QJsonDocument::Compact);

        qDebug() << "Sending webhook update for job:" << jobId << "Progress:" << json[QStringLiteral("progress")].toDouble();

        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:8766/webhook")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QNetworkReply *reply = discordNetworkManager->post(request, payload);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, jobId]() {
            if (reply->error() != QNetworkReply::NoError) {
                bool isHostClosed = (reply->error() == QNetworkReply::RemoteHostClosedError);
                int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                bool isNotFound = (httpStatus == 404);
                
                if (!isHostClosed && !isNotFound) {
                    qWarning() << "Discord Webhook error for job:" << jobId << reply->errorString() << reply->readAll();
                }
            } else {
                int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qDebug() << "Discord Webhook success for job:" << jobId << "HTTP Status:" << statusCode;
            }
            reply->deleteLater();
        });
    };

    QSharedPointer<QMap<QString, QVariantMap>> webhookStates = QSharedPointer<QMap<QString, QVariantMap>>::create();
    QSharedPointer<QMap<QString, qint64>> webhookTimestamps = QSharedPointer<QMap<QString, qint64>>::create();
    QSharedPointer<QList<QString>> queuedJobs = QSharedPointer<QList<QString>>::create();

    auto updateQueuePositions = [sendDiscordWebhook, webhookStates, webhookTimestamps, queuedJobs]() {
        int i = 0;
        for (const QString &qId : *queuedJobs) {
            if (webhookStates->contains(qId)) {
                QVariantMap &qState = (*webhookStates)[qId];
                int oldPos = qState.value(QStringLiteral("queue_position"), 0).toInt();
                int newPos = i + 1;
                if (oldPos != newPos) {
                    qState[QStringLiteral("queue_position")] = newPos;
                    (*webhookTimestamps)[qId] = QDateTime::currentMSecsSinceEpoch();
                    sendDiscordWebhook(qId, qState);
                }
            }
            i++;
        }
    };

    connect(m_downloadManager, &DownloadManager::downloadAddedToQueue, this, [webhookStates, queuedJobs](const QVariantMap &itemData) {
        QString id = itemData.value(QStringLiteral("id")).toString();
        QVariantMap state;
        state[QStringLiteral("url")] = itemData.value(QStringLiteral("url"));
        state[QStringLiteral("title")] = itemData.value(QStringLiteral("title"));
        QVariantMap options = itemData.value(QStringLiteral("options")).toMap();
        state[QStringLiteral("download_type")] = options.value(QStringLiteral("type"), QStringLiteral("video"));
        state[QStringLiteral("status")] = itemData.value(QStringLiteral("status"), QStringLiteral("Queued"));
        
        if (webhookStates->contains(id) && !itemData.contains(QStringLiteral("progress"))) {
            state[QStringLiteral("progress")] = (*webhookStates)[id].value(QStringLiteral("progress"), 0.0).toDouble();
        } else {
            state[QStringLiteral("progress")] = itemData.value(QStringLiteral("progress"), 0.0).toDouble();
        }

        if (options.contains(QStringLiteral("playlist_placeholder_id"))) {
            state[QStringLiteral("parent_id")] = options.value(QStringLiteral("playlist_placeholder_id")).toString();
        }

        QString status = state[QStringLiteral("status")].toString();
        if (status.contains(QStringLiteral("Queued")) || status == QStringLiteral("Checking for playlist...")) {
            if (!queuedJobs->contains(id)) {
                queuedJobs->append(id);
            }
            state[QStringLiteral("queue_position")] = queuedJobs->indexOf(id) + 1;
        } else {
            state[QStringLiteral("queue_position")] = 0;
        }

        (*webhookStates)[id] = state;
    });

    connect(m_downloadManager, &DownloadManager::downloadProgress, this, [sendDiscordWebhook, webhookStates, webhookTimestamps, queuedJobs, updateQueuePositions](const QString &id, const QVariantMap &data) {
        if (!webhookStates->contains(id)) {
            if (data.contains(QStringLiteral("url")) && data.contains(QStringLiteral("options"))) {
                QVariantMap state;
                state[QStringLiteral("url")] = data.value(QStringLiteral("url"));
                state[QStringLiteral("title")] = data.value(QStringLiteral("title"));
                state[QStringLiteral("download_type")] = data.value(QStringLiteral("options")).toMap().value(QStringLiteral("type"), QStringLiteral("video")).toString();
                state[QStringLiteral("status")] = data.value(QStringLiteral("status"), QStringLiteral("Queued")).toString();
                
                state[QStringLiteral("progress")] = data.value(QStringLiteral("progress"), 0.0).toDouble();

                QVariantMap options = data.value(QStringLiteral("options")).toMap();
                if (options.contains(QStringLiteral("playlist_placeholder_id"))) {
                    state[QStringLiteral("parent_id")] = options.value(QStringLiteral("playlist_placeholder_id")).toString();
                }

                QString status = state[QStringLiteral("status")].toString();
                if (status == QStringLiteral("Queued") || status == QStringLiteral("Checking for playlist...")) {
                    if (!queuedJobs->contains(id)) {
                        queuedJobs->append(id);
                    }
                    state[QStringLiteral("queue_position")] = queuedJobs->indexOf(id) + 1;
                } else {
                    state[QStringLiteral("queue_position")] = 0;
                }

                (*webhookStates)[id] = state;
            } else {
                return;
            }
        }

        QVariantMap &state = (*webhookStates)[id];
        QString oldStatus = state.value(QStringLiteral("status")).toString();

        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            state[it.key()] = it.value();
        }

        QString newStatus = state.value(QStringLiteral("status")).toString();
        bool wasQueued = (oldStatus.contains(QStringLiteral("Queued")) || oldStatus == QStringLiteral("Checking for playlist..."));
        bool isQueued = (newStatus.contains(QStringLiteral("Queued")) || newStatus == QStringLiteral("Checking for playlist..."));
        bool queueOrderChanged = false;

        if (wasQueued && !isQueued) {
            if (queuedJobs->removeAll(id) > 0) {
                state[QStringLiteral("queue_position")] = 0;
                queueOrderChanged = true;
            }
        } else if (!wasQueued && isQueued) {
            if (!queuedJobs->contains(id)) {
                queuedJobs->append(id);
                state[QStringLiteral("queue_position")] = queuedJobs->size();
            }
        }

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 lastSent = webhookTimestamps->value(id, 0);

        bool shouldSend = false;
        if (now - lastSent >= 1500) {
            shouldSend = true;
        } else if (data.contains(QStringLiteral("status")) && newStatus != oldStatus) {
            shouldSend = true;
        }

        if (shouldSend) {
            (*webhookTimestamps)[id] = now;
            sendDiscordWebhook(id, state);
        }

        if (queueOrderChanged) {
            updateQueuePositions();
        }
    });

    connect(m_activeDownloadsTab, &ActiveDownloadsTab::moveDownloadUpRequested, this, [queuedJobs, updateQueuePositions](const QString &id) {
        int idx = queuedJobs->indexOf(id);
        if (idx > 0) {
            queuedJobs->swapItemsAt(idx, idx - 1);
            updateQueuePositions();
        }
    });

    connect(m_activeDownloadsTab, &ActiveDownloadsTab::moveDownloadDownRequested, this, [queuedJobs, updateQueuePositions](const QString &id) {
        int idx = queuedJobs->indexOf(id);
        if (idx >= 0 && idx < queuedJobs->size() - 1) {
            queuedJobs->swapItemsAt(idx, idx + 1);
            updateQueuePositions();
        }
    });

    connect(m_downloadManager, &DownloadManager::downloadFinished, this, [sendDiscordWebhook, webhookStates, queuedJobs, updateQueuePositions](const QString &id) {
        if (!webhookStates->contains(id)) return;
        QVariantMap state = (*webhookStates)[id];
        state[QStringLiteral("status")] = QStringLiteral("Completed");
        state[QStringLiteral("progress")] = 100.0;
        state[QStringLiteral("queue_position")] = 0;
        sendDiscordWebhook(id, state);

        if (queuedJobs->removeAll(id) > 0) {
            updateQueuePositions();
        }
    });

    connect(m_downloadManager, &DownloadManager::downloadCancelled, this, [sendDiscordWebhook, webhookStates, queuedJobs, updateQueuePositions](const QString &id) {
        if (!webhookStates->contains(id)) return;
        QVariantMap state = (*webhookStates)[id];
        state[QStringLiteral("status")] = QStringLiteral("Cancelled");
        state[QStringLiteral("queue_position")] = 0;
        sendDiscordWebhook(id, state);

        if (queuedJobs->removeAll(id) > 0) {
            updateQueuePositions();
        }
    });

    connect(m_downloadManager, &DownloadManager::downloadPaused, this, [sendDiscordWebhook, webhookStates, queuedJobs, updateQueuePositions](const QString &id) {
        if (!webhookStates->contains(id)) return;
        QVariantMap state = (*webhookStates)[id];
        state[QStringLiteral("status")] = QStringLiteral("Paused");
        state[QStringLiteral("queue_position")] = 0;
        sendDiscordWebhook(id, state);

        if (queuedJobs->removeAll(id) > 0) {
            updateQueuePositions();
        }
    });

    connect(m_downloadManager, &DownloadManager::downloadResumed, this, [sendDiscordWebhook, webhookStates, queuedJobs, updateQueuePositions](const QString &id) {
        if (!webhookStates->contains(id)) return;
        QVariantMap state = (*webhookStates)[id];
        state[QStringLiteral("status")] = QStringLiteral("Resuming download...");
        state[QStringLiteral("queue_position")] = 0;
        sendDiscordWebhook(id, state);

        if (queuedJobs->removeAll(id) > 0) {
            updateQueuePositions();
        }
    });

    connect(m_downloadManager, &DownloadManager::downloadRemovedFromQueue, this, [webhookStates, webhookTimestamps, queuedJobs, updateQueuePositions](const QString &id) {
        webhookStates->remove(id);
        webhookTimestamps->remove(id);
        if (queuedJobs->removeAll(id) > 0) {
            updateQueuePositions();
        }
    });
}
