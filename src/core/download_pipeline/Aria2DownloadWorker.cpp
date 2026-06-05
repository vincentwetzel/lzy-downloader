#include "Aria2DownloadWorker.h"
#include <QDir>
#include <QRegularExpression>
#include <QFileInfo>
#include <QPointer>
#include <QDebug>
#include "core/YtDlpArgsBuilder.h"

namespace {
bool containsHeader(const QMap<QString, QString> &headers, const QString &name)
{
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString siteSpecificReferer(const QString &url)
{
    if (url.contains(QStringLiteral("bilibili.com"), Qt::CaseInsensitive)) {
        return url;
    }
    if (url.contains(QStringLiteral("bilibili.tv"), Qt::CaseInsensitive)) {
        return QStringLiteral("https://www.bilibili.tv/");
    }
    if (url.contains(QStringLiteral("nicovideo.jp"), Qt::CaseInsensitive)
        || url.contains(QStringLiteral("nico.ms"), Qt::CaseInsensitive)) {
        return url;
    }
    return QString();
}
}

Aria2DownloadWorker::Aria2DownloadWorker(Aria2RpcClient* globalDaemon, QObject* parent)
    : QObject(parent), m_daemon(globalDaemon)
{
    m_extractor = new YtDlpDownloadInfoExtractor(this);
    m_ffmpeg = new FfmpegMuxer(this);
    m_pollTimer = new QTimer(this);

    connect(m_extractor, &YtDlpDownloadInfoExtractor::extractionSuccess, this, &Aria2DownloadWorker::onExtractionSuccess);
    connect(m_extractor, &YtDlpDownloadInfoExtractor::extractionFailed, this, &Aria2DownloadWorker::onExtractionFailed);

    connect(m_ffmpeg, &FfmpegMuxer::mergeSuccess, this, &Aria2DownloadWorker::onMergeSuccess);
    connect(m_ffmpeg, &FfmpegMuxer::mergeFailed, this, &Aria2DownloadWorker::onMergeFailed);

    connect(m_pollTimer, &QTimer::timeout, this, &Aria2DownloadWorker::pollAria2Status);

    // Listen to the global daemon's signals
    connect(m_daemon, &Aria2RpcClient::downloadProgress, this, &Aria2DownloadWorker::onDownloadProgress);
}

Aria2DownloadWorker::~Aria2DownloadWorker() {
    m_pollTimer->stop();
}

void Aria2DownloadWorker::start(const QString& ytDlpPath, const QString& ffmpegPath, const QString& url, const QString& saveDir, ConfigManager* configManager, const QVariantMap& options) {
    m_state = State::Extracting;
    m_ffmpegPath = ffmpegPath;
    m_saveDir = saveDir;
    setProperty("sourceUrl", url);
    m_isCancelled = false;

    emit statusTextChanged(tr("Extracting media information..."));

    YtDlpArgsBuilder builder;
    QStringList extractionArgs = builder.build(configManager, url, options);

    // Remove conflicting args for JSON extraction
    extractionArgs.removeAll(QStringLiteral("--print"));
    extractionArgs.removeAll(QStringLiteral("after_move:filepath"));

    extractionArgs << QStringLiteral("--dump-json");

    m_extractor->extract(ytDlpPath, extractionArgs);
}

void Aria2DownloadWorker::onExtractionSuccess(const QString& title, const QString& thumbnailUrl, const QList<DownloadTarget>& targets, const QString& finalFilename, const QMap<QString, QString>& httpHeaders, const QVariantMap& metadata) {
    m_state = State::Downloading;
    emit statusTextChanged(tr("Downloading %1 segment(s)...").arg(targets.size()));

    m_title = title;
    m_thumbnailUrl = thumbnailUrl;
    m_downloadedParts.clear();
    m_downloadedSubtitles.clear();
    m_finalFileName = finalFilename;
    QMap<QString, QString> effectiveHttpHeaders = httpHeaders;
    const QString referer = siteSpecificReferer(property("sourceUrl").toString());
    if (!referer.isEmpty() && !containsHeader(effectiveHttpHeaders, QStringLiteral("Referer"))) {
        effectiveHttpHeaders.insert(QStringLiteral("Referer"), referer);
    }

    // Store metadata for the DownloadManager to retrieve later, bypassing brittle JSON disk reads
    this->setProperty("metadata", metadata);

    // Write standard .info.json for consistency and cancellation cleanups
    QString infoFilePath = QDir(m_saveDir).filePath(QFileInfo(m_finalFileName).completeBaseName() + QStringLiteral(".info.json"));
    QFile infoFile(infoFilePath);
    if (infoFile.open(QIODevice::WriteOnly)) { infoFile.write(QJsonDocument::fromVariant(metadata).toJson()); infoFile.close(); }

    int expectedGids = static_cast<int>(targets.size());
    const bool hasThumbnail = !m_thumbnailUrl.isEmpty();

    if (hasThumbnail) {
        expectedGids++;
        m_thumbnailPath = QDir(m_saveDir).filePath(QFileInfo(m_finalFileName).completeBaseName() + QStringLiteral("_thumb.jpg"));
    }

    // Using QPointer ensures callbacks don't crash if this worker is destroyed early
    QPointer<Aria2DownloadWorker> ptr(this);
    auto checkStartPolling = [ptr, expectedGids]() {
        if (ptr && ptr->m_activeGids.size() == expectedGids) {
            ptr->m_pollTimer->start(500); // Poll every 500ms
        }
    };

    for (const DownloadTarget& target : targets) {
        QString partFilePath = QDir(m_saveDir).filePath(target.filename);
        if (target.type == DownloadTarget::Type::Subtitle) {
            m_downloadedSubtitles.append({partFilePath, target.lang});
        } else {
            m_downloadedParts.append(partFilePath);
        }

        // Tell aria2c to start downloading this part
        m_daemon->addDownload(target.url, m_saveDir, target.filename, effectiveHttpHeaders, [ptr, checkStartPolling](const QString& gid) {
            if (ptr) {
                if (ptr->m_state != State::Error) {
                    ptr->m_activeGids.append(gid);
                    ptr->m_allGids.append(gid);
                    checkStartPolling();
                } else {
                    ptr->m_daemon->removeDownload(gid);
                }
            }
        }, [ptr](const QString& err) {
            if (ptr && ptr->m_state != State::Error) {
                ptr->m_state = State::Error;
                for (const QString& activeGid : ptr->m_activeGids) {
                    ptr->m_daemon->removeDownload(activeGid);
                }
                ptr->cleanupPartialFiles();
                emit ptr->error(tr("Aria2 rejected download: %1").arg(err));
            }
        });
    }

    if (hasThumbnail) {
        m_daemon->addDownload(m_thumbnailUrl, m_saveDir, QFileInfo(m_thumbnailPath).fileName(), effectiveHttpHeaders, [ptr, checkStartPolling](const QString& gid) {
            if (ptr) {
                if (ptr->m_state != State::Error) {
                    ptr->m_activeGids.append(gid);
                    ptr->m_allGids.append(gid);
                    checkStartPolling();
                } else {
                    ptr->m_daemon->removeDownload(gid);
                }
            }
        }, [ptr](const QString& err) {
            if (ptr && ptr->m_state != State::Error) {
                ptr->m_state = State::Error;
                for (const QString& activeGid : ptr->m_activeGids) {
                    ptr->m_daemon->removeDownload(activeGid);
                }
                ptr->cleanupPartialFiles();
                emit ptr->error(tr("Aria2 rejected thumbnail: %1").arg(err));
            }
        });
    }
}

void Aria2DownloadWorker::onExtractionFailed(const QString& errorMsg) {
    if (m_isCancelled) return;

    m_state = State::Error;
    emit error(errorMsg);
}

void Aria2DownloadWorker::pollAria2Status() {
    for (const QString& gid : m_activeGids) {
        m_daemon->queryStatus(gid);
    }
}

void Aria2DownloadWorker::onDownloadProgress(const QString& gid, qint64 completedLength, qint64 totalLength, qint64 downloadSpeed, const QString& status) {
    if (!m_allGids.contains(gid)) return; // Belongs to another worker

    if (m_state == State::Error || m_isCancelled) return;

    if (status == QStringLiteral("error")) {
        m_pollTimer->stop();
        for (const QString& activeGid : m_activeGids) {
            m_daemon->removeDownload(activeGid);
        }
        cleanupPartialFiles();
        m_state = State::Error;
        emit error(tr("Aria2c encountered an error downloading a segment."));
        return;
    }

    m_completedLengths[gid] = completedLength;
    m_totalLengths[gid] = totalLength;
    m_downloadSpeeds[gid] = (status == QStringLiteral("complete")) ? 0 : downloadSpeed;

    qint64 totalCompleted = 0;
    qint64 totalSize = 0;
    qint64 totalSpeed = 0;

    for (const QString& trackedGid : m_allGids) {
        totalCompleted += m_completedLengths.value(trackedGid, 0);
        totalSize += m_totalLengths.value(trackedGid, 0);
        totalSpeed += m_downloadSpeeds.value(trackedGid, 0);
    }

    if (totalSize > 0) {
        const int percent = static_cast<int>((totalCompleted * 100) / totalSize);
        const QString speedStr = totalSpeed > 1048576 ? tr("%1 MB/s").arg(QString::number(totalSpeed / 1048576.0, 'f', 2)) : tr("%1 KB/s").arg(QString::number(totalSpeed / 1024));
        emit progressUpdated(percent, speedStr);
        emit statusTextChanged(tr("Downloading..."));
    }

    if (status == QStringLiteral("complete") && m_activeGids.contains(gid)) {
        m_activeGids.removeOne(gid);

        if (m_activeGids.isEmpty()) {
            // All parts downloaded! Move to next state.
            m_pollTimer->stop();
            m_state = State::PostProcessing;

            emit statusTextChanged(tr("Merging segments with ffmpeg..."));
            emit progressUpdated(100, tr("Processing"));

            QString finalPath = QDir(m_saveDir).filePath(m_finalFileName);
            m_ffmpeg->merge(m_ffmpegPath, m_downloadedParts, finalPath, m_title, m_thumbnailPath, m_downloadedSubtitles);
        }
    }
}

void Aria2DownloadWorker::onMergeSuccess(const QString& outputFile) {
    m_state = State::Finished;
    emit statusTextChanged(tr("Done"));
    emit finished(outputFile);
}

void Aria2DownloadWorker::onMergeFailed(const QString& errorMsg) {
    if (m_isCancelled) return;

    cleanupPartialFiles();
    m_state = State::Error;
    emit error(errorMsg);
}

void Aria2DownloadWorker::cancel() {
    if (m_state == State::Finished || m_state == State::Error || m_state == State::Idle) return;

    m_isCancelled = true;
    m_pollTimer->stop();

    if (m_state == State::Extracting) {
        m_extractor->cancel();
    } else if (m_state == State::Downloading) {
        for (const QString& gid : m_activeGids) {
            m_daemon->removeDownload(gid);
        }
    } else if (m_state == State::PostProcessing) {
        m_ffmpeg->cancel();
    }

    cleanupPartialFiles();

    m_state = State::Error;
    emit statusTextChanged(tr("Cancelled"));
}

void Aria2DownloadWorker::cleanupPartialFiles() {
    const QStringList parts = m_downloadedParts;
    const QList<SubtitleFile> subs = m_downloadedSubtitles;
    const QString thumb = m_thumbnailPath;
    const QString info = m_finalFileName.isEmpty() ? QString() : QDir(m_saveDir).filePath(QFileInfo(m_finalFileName).completeBaseName() + QStringLiteral(".info.json"));

    // Delay file deletion so aria2c/ffmpeg have time to release OS file locks
    QTimer::singleShot(500, [parts, subs, thumb, info]() {
        for (const QString& partFile : parts) {
            QFile::remove(partFile);
            QFile::remove(partFile + QStringLiteral(".aria2"));
        }
        for (const SubtitleFile& subFile : subs) {
            QFile::remove(subFile.path);
            QFile::remove(subFile.path + QStringLiteral(".aria2"));
        }
        if (!thumb.isEmpty()) QFile::remove(thumb);
        if (!info.isEmpty()) QFile::remove(info);
    });
}
