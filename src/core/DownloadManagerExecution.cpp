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
#include <QDateTime>
#include <chrono>

namespace {
QString youtubeVideoIdFromUrl(const QString &urlString)
{
    const QUrl url(urlString);
    const QString host = url.host().toLower();
    if (host.contains(QStringLiteral("youtu.be"))) {
        const QString id = url.path().section('/', 1, 1);
        return id.left(11);
    }

    if (host.contains(QStringLiteral("youtube.com")) || host.contains(QStringLiteral("youtube-nocookie.com"))) {
        const QString queryId = QUrlQuery(url).queryItemValue(QStringLiteral("v"));
        if (!queryId.isEmpty()) {
            return queryId.left(11);
        }

        const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
        for (int i = 0; i + 1 < parts.size(); ++i) {
            const QString marker = parts.at(i).toLower();
            if (marker == QStringLiteral("shorts") || marker == QStringLiteral("embed") || marker == QStringLiteral("live")) {
                return parts.at(i + 1).left(11);
            }
        }
    }

    if (!host.isEmpty()) {
        return QString();
    }

    static const QRegularExpression idPattern(QStringLiteral(R"(([A-Za-z0-9_-]{11}))"));
    const QRegularExpressionMatch match = idPattern.match(urlString);
    return match.hasMatch() ? match.captured(1) : QString();
}

QUrl sponsorBlockSegmentsUrl(const QString &videoId)
{
    const QByteArray hash = QCryptographicHash::hash(videoId.toUtf8(), QCryptographicHash::Sha256).toHex();
    QUrl url(QStringLiteral("https://sponsor.ajay.app/api/skipSegments/%1").arg(QString::fromLatin1(hash.left(4))));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("service"), QStringLiteral("YouTube"));
    query.addQueryItem(QStringLiteral("categories"), QStringLiteral(R"(["preview","intro","selfpromo","interaction","filler","music_offtopic","sponsor","outro","hook"])"));
    query.addQueryItem(QStringLiteral("actionTypes"), QStringLiteral(R"(["skip","poi","chapter"])"));
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
        if (!object.value(QStringLiteral("videoID")).toString().isEmpty() && object.value(QStringLiteral("videoID")).toString() != videoId) {
            continue;
        }
        if (object.value(QStringLiteral("segments")).isArray()) {
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

    const QString downloadType = item.options.value(QStringLiteral("type"), QStringLiteral("video")).toString();

    if (downloadType == QStringLiteral("gallery")) {
        item.options.insert(QStringLiteral("id"), item.id);
        item.options.insert(QStringLiteral("playlist_index"), item.playlistIndex);
        GalleryDlArgsBuilder argsBuilder(m_configManager);
        const QStringList args = argsBuilder.build(item.url, item.options);

        GalleryDlWorker *worker = new GalleryDlWorker(item.id, args, m_configManager, this);
        m_activeWorkers.insert(item.id, worker);
        m_activeItems.insert(item.id, item);

        connect(worker, &GalleryDlWorker::progressUpdated, this, &DownloadManager::onWorkerProgress);
        connect(worker, &GalleryDlWorker::finished, this, &DownloadManager::onGalleryDlWorkerFinished);
        connect(worker, &GalleryDlWorker::outputReceived, this, &DownloadManager::onWorkerOutputReceived);

        emit downloadStarted(item.id);
        worker->start();
    } else {
        item.options.insert(QStringLiteral("id"), item.id);
        item.options.insert(QStringLiteral("playlist_index"), item.playlistIndex);
        YtDlpArgsBuilder argsBuilder;
        const QStringList args = argsBuilder.build(m_configManager, item.url, item.options);

        YtDlpWorker *worker = new YtDlpWorker(item.id, args, m_configManager, this);
        m_activeWorkers.insert(item.id, worker);
        m_activeItems.insert(item.id, item);

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
    if (!m_configManager->get(QStringLiteral("General"), QStringLiteral("sponsorblock"), false).toBool()) {
        return false;
    }
    if (item.options.value(QStringLiteral("sponsorblock_segments_checked"), false).toBool()) {
        return false;
    }

    const QString downloadType = item.options.value(QStringLiteral("type"), QStringLiteral("video")).toString();
    const bool isLivestream = item.options.value(QStringLiteral("is_live"), false).toBool()
        || item.options.value(QStringLiteral("wait_for_video"), false).toBool();
    if (downloadType != QStringLiteral("video") && !isLivestream) {
        return false;
    }

    return !youtubeVideoIdFromUrl(item.url).isEmpty();
}

void DownloadManager::startSponsorBlockPreflight(const DownloadItem &item) {
    const QString videoId = youtubeVideoIdFromUrl(item.url);
    if (videoId.isEmpty()) {
        DownloadItem fallbackItem = item;
        fallbackItem.options.insert(QStringLiteral("sponsorblock_segments_checked"), false);
        startDownloadItem(fallbackItem, true);
        return;
    }

    m_pendingSponsorBlockPreflights.insert(item.id, item);
    m_activeItems.insert(item.id, item); // Track as active so it saves to the queue backup

    QVariantMap progressData;
    progressData.insert(QStringLiteral("status"), tr("Checking SponsorBlock segments..."));
    progressData.insert(QStringLiteral("progress"), -1);
    emit downloadProgress(item.id, progressData);

    QNetworkAccessManager *networkManager = new QNetworkAccessManager(this);
    QNetworkRequest request(sponsorBlockSegmentsUrl(videoId));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
    QNetworkReply *reply = networkManager->get(request);
    QTimer::singleShot(std::chrono::seconds(8), reply, [reply]() {
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

        checkedItem.options.insert(QStringLiteral("sponsorblock_segments_checked"), checked);
        checkedItem.options.insert(QStringLiteral("sponsorblock_has_segments"), hasSegments);

        qInfo() << "SponsorBlock preflight for" << videoId
                << "checked=" << checked
                << "hasSegments=" << hasSegments;

        startDownloadItem(checkedItem, true);
        reply->deleteLater();
        networkManager->deleteLater();
    });
}

void DownloadManager::applyMaxConcurrentSetting(const QString &maxThreadsStr) {
    const SleepMode oldSleepMode = m_sleepMode;

    if (maxThreadsStr == QStringLiteral("1 (short sleep)")) {
        m_maxConcurrentDownloads = 1;
        m_sleepMode = ShortSleep;
    } else if (maxThreadsStr == QStringLiteral("1 (long sleep)")) {
        m_maxConcurrentDownloads = 1;
        m_sleepMode = LongSleep;
    } else {
        m_maxConcurrentDownloads = qMax(1, maxThreadsStr.toInt());
        m_sleepMode = NoSleep;
    }

    if (oldSleepMode != m_sleepMode && m_sleepTimer && m_sleepTimer->isActive()) {
        m_sleepTimer->stop();
    }
}

void DownloadManager::startDownloadsToCapacity() {
    if (m_sleepTimer->isActive()) {
        if (!m_queueManager->hasQueuedDownloads()) {
            m_sleepTimer->stop();
            checkQueueFinished();
        }
        return;
    }

    if (m_sleepMode != NoSleep && m_maxConcurrentDownloads == 1 && 
        m_queueManager->hasQueuedDownloads() && 
        (m_activeWorkers.count() + m_pendingSponsorBlockPreflights.count()) < m_maxConcurrentDownloads) {
        
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 sleepDuration = (m_sleepMode == ShortSleep) ? 5000 : 30000;
        const qint64 timeSinceLastFinish = now - m_lastDownloadFinishTime;

        if (m_lastDownloadFinishTime > 0 && timeSinceLastFinish < sleepDuration) {
            const int remainingSleep = static_cast<int>(sleepDuration - qMax(Q_INT64_C(0), timeSinceLastFinish));
            qDebug() << "Starting sleep timer for remaining" << remainingSleep << "ms.";
            m_sleepTimer->start(std::chrono::milliseconds(remainingSleep));
            return;
        }
    }

    while ((m_activeWorkers.count() + m_pendingSponsorBlockPreflights.count()) < m_maxConcurrentDownloads && m_queueManager->hasQueuedDownloads()) {
        proceedWithDownload();
    }

    checkQueueFinished();
}

void DownloadManager::startNextDownload() {
    startDownloadsToCapacity();
}

void DownloadManager::onSleepTimerTimeout() {
    m_sleepTimer->stop();
    qDebug() << "Sleep timer timed out. Attempting to start next download.";
    startNextDownload();
}
