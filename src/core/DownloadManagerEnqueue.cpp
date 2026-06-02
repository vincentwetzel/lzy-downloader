#include "DownloadManager.h"
#include "DownloadQueueManager.h"
#include "PlaylistExpander.h"
#include "core/ProcessUtils.h"
#include <QUuid>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>

namespace {
bool isNonInteractiveRequest(const QVariantMap &options)
{
    return options.value(QStringLiteral("non_interactive"), false).toBool();
}
}

void DownloadManager::enqueueDownload(const QString &url, const QVariantMap &options) {
    QVariantMap effectiveOptions = options;
    if (isNonInteractiveRequest(effectiveOptions)) {
        effectiveOptions[QStringLiteral("override_archive")] = true;
        effectiveOptions[QStringLiteral("playlist_logic")] = QStringLiteral("Download All (no prompt)");
        effectiveOptions[QStringLiteral("runtime_format_selected")] = true;
        effectiveOptions[QStringLiteral("download_sections_set")] = true;
    }

    // Check if URL is already in any state (prevents duplicate enqueuing)
    bool overrideArchive = effectiveOptions.value(QStringLiteral("override_archive"), false).toBool();
    DownloadQueueManager::DuplicateStatus status = m_queueManager->getDuplicateStatus(url, m_activeItems);
    
    if (status != DownloadQueueManager::NotDuplicate) {
        // If it's only in completed and override is enabled, allow it
        if (status == DownloadQueueManager::DuplicateCompleted && overrideArchive) {
            qDebug() << "DownloadManager: Allowing re-download of completed URL (override enabled):" << url;
        } else {
            QString reason;
            switch (status) {
                case DownloadQueueManager::DuplicateInQueue:
                    reason = tr("This URL is already waiting in the download queue.");
                    break;
                case DownloadQueueManager::DuplicateActive:
                    reason = tr("This URL is currently being downloaded.");
                    break;
                case DownloadQueueManager::DuplicatePaused:
                    reason = tr("This download is paused.");
                    break;
                case DownloadQueueManager::DuplicateCompleted:
                    reason = tr("This URL has already been downloaded (use 'Override duplicate check' to re-download).");
                    break;
                default:
                    reason = tr("This URL is already in the system.");
                    break;
            }
            qDebug() << "DownloadManager: Skipping duplicate URL:" << url << "- Reason:" << reason;
            emit duplicateDownloadDetected(url, reason);
            return;
        }
    }

    // Intercept for download sections before anything else
    bool useSections = m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("download_sections_enabled"), false).toBool();
    QString downloadTypeCheck = effectiveOptions.value(QStringLiteral("type"), QStringLiteral("video")).toString();
    // The "download_sections_set" flag prevents an infinite loop.
    if (useSections && !isNonInteractiveRequest(effectiveOptions) && !effectiveOptions.contains(QStringLiteral("download_sections_set")) && (downloadTypeCheck == QStringLiteral("video") || downloadTypeCheck == QStringLiteral("audio"))) {
        qDebug() << "Download sections enabled, fetching metadata for" << url;
        fetchInfoForSections(url, effectiveOptions);
        return;
    }

    QString downloadType = effectiveOptions.value(QStringLiteral("type"), QStringLiteral("video")).toString();

    // Intercept for runtime format selection before doing anything else
    bool needsRuntimeSelection = false;
    if (downloadType == QStringLiteral("video")) {
        if (m_configManager->get(QStringLiteral("Video"), QStringLiteral("video_quality")).toString() == QStringLiteral("Select at Runtime")) {
            needsRuntimeSelection = true;
        }
    } else if (downloadType == QStringLiteral("audio")) {
        if (m_configManager->get(QStringLiteral("Audio"), QStringLiteral("audio_quality")).toString() == QStringLiteral("Select at Runtime")) {
            needsRuntimeSelection = true;
        }
    }

    if (needsRuntimeSelection && !isNonInteractiveRequest(effectiveOptions) && !effectiveOptions.value(QStringLiteral("runtime_format_selected"), false).toBool()) {
        fetchFormatsForSelection(url, effectiveOptions);
        return;
    }

    if (downloadType == QStringLiteral("gallery")) {
        DownloadItem item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item.url = url;
        item.options = effectiveOptions;
        item.playlistIndex = -1; // Not part of a playlist initially

        m_queueManager->enqueueDownload(item);
    } else {
        // IMMEDIATE UI FEEDBACK: Create the download item in UI before playlist expansion
        DownloadItem item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item.url = url;
        item.options = effectiveOptions;
        item.playlistIndex = -1;

        QVariantMap uiData;
        uiData[QStringLiteral("id")] = item.id;
        uiData[QStringLiteral("url")] = url;
        uiData[QStringLiteral("status")] = tr("Checking for playlist...");
        uiData[QStringLiteral("options")] = effectiveOptions;
        emit downloadAddedToQueue(uiData);

        PlaylistExpander *expander = new PlaylistExpander(url, m_configManager, this);
        expander->setProperty("options", effectiveOptions);
        expander->setProperty("queueId", item.id); // Store queue ID for later

        // Mark the placeholder as pending before it enters the queue so the
        // queue cannot start downloading the original playlist URL first.
        m_queueManager->m_pendingExpansions[item.id] = url;
        m_queueManager->enqueueDownload(item, false); // Enqueue as placeholder, not a "new" item for UI
        connect(expander, &PlaylistExpander::expansionFinished, this, &DownloadManager::onPlaylistExpanded);
        connect(expander, &PlaylistExpander::playlistDetected, this, &DownloadManager::onPlaylistDetected);

        QString playlistLogic = effectiveOptions.value(QStringLiteral("playlist_logic"), QStringLiteral("Ask")).toString();
        expander->startExpansion(playlistLogic);
        emit playlistExpansionStarted(url);
    }
}

void DownloadManager::fetchInfoForSections(const QString &url, const QVariantMap &options)
{
    QProcess *process = new QProcess(this);
    QString ytDlpPath = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager).path;

    QStringList args;
    args << QStringLiteral("--dump-json") << QStringLiteral("--no-playlist") << url;

    QString cookiesBrowser = m_configManager->get(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), QStringLiteral("None")).toString();
    if (cookiesBrowser != QStringLiteral("None")) {
        args << QStringLiteral("--cookies-from-browser") << cookiesBrowser.toLower();
    }

    connect(process, &QProcess::finished, this, [this, process, url, options](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            QByteArray output = process->readAllStandardOutput();
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(output, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QVariantMap infoJson = doc.object().toVariantMap();
                QMetaObject::invokeMethod(this, [this, url, options, infoJson]() {
                    emit downloadSectionsRequested(url, options, infoJson);
                }, Qt::QueuedConnection);
            } else {
                qWarning() << "Failed to parse JSON for sections, enqueuing without them. Error:" << parseError.errorString();
                QVariantMap newOptions = options;
                newOptions[QStringLiteral("download_sections_set")] = true; // Prevent re-triggering
                enqueueDownload(url, newOptions);
            }
        } else {
            qWarning() << "Failed to fetch info for sections, enqueuing without them. Error:" << process->readAllStandardError();
            QVariantMap newOptions = options;
            newOptions[QStringLiteral("download_sections_set")] = true; // Prevent re-triggering
            enqueueDownload(url, newOptions);
        }
        process->deleteLater();
    });
    
    connect(process, &QProcess::errorOccurred, this, [this, process, url, options](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning() << "Failed to start yt-dlp for sections info, enqueuing without them.";
            QVariantMap newOptions = options;
            newOptions[QStringLiteral("download_sections_set")] = true; // Prevent re-triggering
            enqueueDownload(url, newOptions);
            process->deleteLater();
        }
    });

    process->start(ytDlpPath, args);
}

void DownloadManager::fetchFormatsForSelection(const QString &url, const QVariantMap &options) {
    QProcess *process = new QProcess(this);
    QString ytDlpPath = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager).path;
    
    QStringList args;
    args << QStringLiteral("--dump-json") << QStringLiteral("--no-playlist") << url;
    
    QString cookiesBrowser = m_configManager->get(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), QStringLiteral("None")).toString();
    if (cookiesBrowser != QStringLiteral("None")) {
        args << QStringLiteral("--cookies-from-browser") << cookiesBrowser.toLower();
    }
    
    connect(process, &QProcess::finished, this, [this, process, url, options](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            QByteArray output = process->readAllStandardOutput();
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(output, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QVariantMap metadata = doc.object().toVariantMap();
                QVariantMap newOptions = options;
                if (metadata.value(QStringLiteral("is_live"), false).toBool()) {
                    newOptions[QStringLiteral("is_live")] = true;
                }
                QMetaObject::invokeMethod(this, [this, url, newOptions, metadata]() {
                    emit formatSelectionRequested(url, newOptions, metadata);
                }, Qt::QueuedConnection);
            } else {
                const QString message = tr("yt-dlp returned invalid format metadata: %1").arg(parseError.errorString());
                qWarning() << "DownloadManager: Invalid JSON returned from yt-dlp -J:" << parseError.errorString();
                QMetaObject::invokeMethod(this, [this, url, message]() {
                    emit formatSelectionFailed(url, message);
                }, Qt::QueuedConnection);
            }
        } else {
            const QString errorText = QString::fromUtf8(process->readAllStandardError()).trimmed();
            qWarning() << "DownloadManager: yt-dlp -J failed:" << errorText;
            QString message = errorText.isEmpty() ? tr("Failed to retrieve available formats.") : errorText;
            QMetaObject::invokeMethod(this, [this, url, message]() {
                emit formatSelectionFailed(url, message);
            }, Qt::QueuedConnection);
        }
        process->deleteLater();
    });
    
    connect(process, &QProcess::errorOccurred, this, [this, process, url](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning() << "yt-dlp failed to start for formats lookup.";
            QMetaObject::invokeMethod(this, [this, url]() {
                emit formatSelectionFailed(url, tr("Failed to start yt-dlp process."));
            }, Qt::QueuedConnection);
            process->deleteLater();
        }
    });

    process->start(ytDlpPath, args);
}

void DownloadManager::resumeDownloadWithFormat(const QString &url, const QVariantMap &options, const QString &formatId) {
    QVariantMap newOptions = options;
    newOptions[QStringLiteral("runtime_format_selected")] = true;
    if (options.value(QStringLiteral("type"), QStringLiteral("video")).toString() == QStringLiteral("audio")) {
        newOptions[QStringLiteral("runtime_audio_format")] = formatId;
    } else {
        newOptions[QStringLiteral("runtime_video_format")] = formatId;
    }
    enqueueDownload(url, newOptions);
}
