#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "GalleryDlArgsBuilder.h"
#include "GalleryDlWorker.h"
#include "YtDlpArgsBuilder.h"
#include "YtDlpWorker.h"
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
QString youtubeVideoIdFromUrl(const QString &urlString)
{
    const QUrl url(urlString);
    const QString host = url.host().toLower();
    if (host.contains("youtu.be")) {
        const QString id = url.path().section('/', 1, 1);
        return id.left(11);
    }

    if (host.contains("youtube.com") || host.contains("youtube-nocookie.com")) {
        const QString queryId = QUrlQuery(url).queryItemValue("v");
        if (!queryId.isEmpty()) {
            return queryId.left(11);
        }

        const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
        for (int i = 0; i + 1 < parts.size(); ++i) {
            const QString marker = parts.at(i).toLower();
            if (marker == "shorts" || marker == "embed" || marker == "live") {
                return parts.at(i + 1).left(11);
            }
        }
    }

    if (!host.isEmpty()) {
        return QString();
    }

    static const QRegularExpression idPattern(R"(([A-Za-z0-9_-]{11}))");
    const QRegularExpressionMatch match = idPattern.match(urlString);
    return match.hasMatch() ? match.captured(1) : QString();
}

QUrl sponsorBlockSegmentsUrl(const QString &videoId)
{
    const QByteArray hash = QCryptographicHash::hash(videoId.toUtf8(), QCryptographicHash::Sha256).toHex();
    QUrl url(QString("https://sponsor.ajay.app/api/skipSegments/%1").arg(QString::fromLatin1(hash.left(4))));
    QUrlQuery query;
    query.addQueryItem("service", "YouTube");
    query.addQueryItem("categories", R"(["preview","intro","selfpromo","interaction","filler","music_offtopic","sponsor","outro","hook"])");
    query.addQueryItem("actionTypes", R"(["skip","poi","chapter"])");
    url.setQuery(query);
    return url;
}

bool sponsorBlockResponseHasSegmentsForVideo(const QByteArray &data, const QString &videoId)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return false;
    }

    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        if (!object.value("videoID").toString().isEmpty() && object.value("videoID").toString() != videoId) {
            continue;
        }
        if (object.value("segment").isArray()) {
            return true;
        }
    }

    return false;
}
}

void DownloadManager::proceedWithDownload() {
    if (!m_queueManager->hasQueuedDownloads()) {
        checkQueueFinished();
        return;
    }

    DownloadItem item = m_queueManager->takeNextQueuedDownload();
    m_activeDownloadsCount++;

    if (shouldPreflightSponsorBlock(item)) {
        startSponsorBlockPreflight(item);
        emitDownloadStats();
        return;
    }

    startDownloadItem(item, true);
}

void DownloadManager::startDownloadItem(DownloadItem item, bool alreadyCountedActive) {
    if (!alreadyCountedActive) {
        m_activeDownloadsCount++;
    }

    QString downloadType = item.options.value("type", "video").toString();

    if (downloadType == "gallery") {
        item.options["id"] = item.id;
        item.options["playlist_index"] = item.playlistIndex;
        GalleryDlArgsBuilder argsBuilder(m_configManager);
        QStringList args = argsBuilder.build(item.url, item.options);

        GalleryDlWorker *worker = new GalleryDlWorker(item.id, args, m_configManager, this);
        m_activeWorkers[item.id] = worker;
        m_activeItems[item.id] = item;

        connect(worker, &GalleryDlWorker::progressUpdated, this, &DownloadManager::onWorkerProgress);
        connect(worker, &GalleryDlWorker::finished, this, &DownloadManager::onGalleryDlWorkerFinished);
        connect(worker, &GalleryDlWorker::outputReceived, this, &DownloadManager::onWorkerOutputReceived);

        emit downloadStarted(item.id);
        worker->start();
    } else {
        item.options["id"] = item.id;
        item.options["playlist_index"] = item.playlistIndex;
        YtDlpArgsBuilder argsBuilder;
        QStringList args = argsBuilder.build(m_configManager, item.url, item.options);

        YtDlpWorker *worker = new YtDlpWorker(item.id, args, m_configManager, this);
        m_activeWorkers[item.id] = worker;
        m_activeItems[item.id] = item;

        connect(worker, &YtDlpWorker::progressUpdated, this, &DownloadManager::onWorkerProgress);
        connect(worker, &YtDlpWorker::finished, this, &DownloadManager::onWorkerFinished);
        connect(worker, &YtDlpWorker::outputReceived, this, &DownloadManager::onWorkerOutputReceived);
        connect(worker, &YtDlpWorker::ytDlpErrorDetected, this, &DownloadManager::onYtDlpErrorDetected);

        emit downloadStarted(item.id);
        worker->start();
    }
    emitDownloadStats();
}

bool DownloadManager::shouldPreflightSponsorBlock(const DownloadItem &item) const {
    if (!m_configManager->get("General", "sponsorblock", false).toBool()) {
        return false;
    }
    if (item.options.value("sponsorblock_segments_checked", false).toBool()) {
        return false;
    }

    const QString downloadType = item.options.value("type", "video").toString();
    const bool isLivestream = item.options.value("is_live", false).toBool()
        || item.options.value("wait_for_video", false).toBool();
    if (downloadType != "video" && !isLivestream) {
        return false;
    }

    return !youtubeVideoIdFromUrl(item.url).isEmpty();
}

void DownloadManager::startSponsorBlockPreflight(const DownloadItem &item) {
    const QString videoId = youtubeVideoIdFromUrl(item.url);
    if (videoId.isEmpty()) {
        DownloadItem fallbackItem = item;
        fallbackItem.options["sponsorblock_segments_checked"] = false;
        startDownloadItem(fallbackItem, true);
        return;
    }

    m_pendingSponsorBlockPreflights[item.id] = item;

    QVariantMap progressData;
    progressData["status"] = "Checking SponsorBlock segments...";
    progressData["progress"] = -1;
    emit downloadProgress(item.id, progressData);

    QNetworkAccessManager *networkManager = new QNetworkAccessManager(this);
    QNetworkReply *reply = networkManager->get(QNetworkRequest(sponsorBlockSegmentsUrl(videoId)));
    QTimer::singleShot(8000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });

    const QString itemId = item.id;
    connect(reply, &QNetworkReply::finished, this, [this, reply, networkManager, videoId, itemId]() {
        if (m_isShuttingDown || !m_pendingSponsorBlockPreflights.contains(itemId)) {
            reply->deleteLater();
            networkManager->deleteLater();
            return;
        }

        DownloadItem checkedItem = m_pendingSponsorBlockPreflights.take(itemId);
        bool checked = false;
        bool hasSegments = false;

        const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int httpStatus = statusAttr.isValid() ? statusAttr.toInt() : 0;
        const QNetworkReply::NetworkError error = reply->error();

        if (error == QNetworkReply::NoError) {
            checked = true;
            hasSegments = sponsorBlockResponseHasSegmentsForVideo(reply->readAll(), videoId);
        } else if (httpStatus == 404) {
            checked = true;
            hasSegments = false;
        } else {
            qWarning() << "SponsorBlock preflight failed for" << videoId << ":" << reply->errorString()
                       << "- falling back to accurate cut arguments.";
        }

        checkedItem.options["sponsorblock_segments_checked"] = checked;
        checkedItem.options["sponsorblock_has_segments"] = hasSegments;

        qInfo() << "SponsorBlock preflight for" << videoId
                << "checked=" << checked
                << "hasSegments=" << hasSegments;

        startDownloadItem(checkedItem, true);
        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void DownloadManager::applyMaxConcurrentSetting(const QString &maxThreadsStr) {
    if (maxThreadsStr == "1 (short sleep)") {
        m_maxConcurrentDownloads = 1;
        m_sleepMode = ShortSleep;
    } else if (maxThreadsStr == "1 (long sleep)") {
        m_maxConcurrentDownloads = 1;
        m_sleepMode = LongSleep;
    } else {
        m_maxConcurrentDownloads = qMax(1, maxThreadsStr.toInt());
        m_sleepMode = NoSleep;
    }
}

void DownloadManager::startDownloadsToCapacity() {
    while ((m_activeWorkers.count() + m_pendingSponsorBlockPreflights.count()) < m_maxConcurrentDownloads && m_queueManager->hasQueuedDownloads()) {
        if (m_sleepMode != NoSleep && m_maxConcurrentDownloads == 1) {
            if (!m_sleepTimer->isActive()) {
                int sleepDuration = (m_sleepMode == ShortSleep) ? 5000 : 30000;
                qDebug() << "Starting sleep timer for" << sleepDuration << "ms.";
                m_sleepTimer->start(sleepDuration);
            }
            return;
        }

        proceedWithDownload();
    }

    checkQueueFinished();
}

void DownloadManager::startNextDownload() {
    startDownloadsToCapacity();
}

void DownloadManager::onSleepTimerTimeout() {
    qDebug() << "Sleep timer timed out. Attempting to start next download.";
    startNextDownload();
}


