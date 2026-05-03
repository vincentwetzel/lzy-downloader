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
    return options.value("non_interactive", false).toBool();
}
}

void DownloadManager::enqueueDownload(const QString &url, const QVariantMap &options) {
    QVariantMap effectiveOptions = options;
    if (isNonInteractiveRequest(effectiveOptions)) {
        effectiveOptions["override_archive"] = true;
        effectiveOptions["playlist_logic"] = "Download All (no prompt)";
        effectiveOptions["runtime_format_selected"] = true;
        effectiveOptions["download_sections_set"] = true;
    }

    // Check if URL is already in any state (prevents duplicate enqueuing)
    bool overrideArchive = effectiveOptions.value("override_archive", false).toBool();
    DownloadQueueManager::DuplicateStatus status = m_queueManager->getDuplicateStatus(url, m_activeItems);
    
    if (status != DownloadQueueManager::NotDuplicate) {
        // If it's only in completed and override is enabled, allow it
        if (status == DownloadQueueManager::DuplicateCompleted && overrideArchive) {
            qDebug() << "DownloadManager: Allowing re-download of completed URL (override enabled):" << url;
        } else {
            QString reason;
            switch (status) {
                case DownloadQueueManager::DuplicateInQueue:
                    reason = "This URL is already waiting in the download queue.";
                    break;
                case DownloadQueueManager::DuplicateActive:
                    reason = "This URL is currently being downloaded.";
                    break;
                case DownloadQueueManager::DuplicatePaused:
                    reason = "This download is paused.";
                    break;
                case DownloadQueueManager::DuplicateCompleted:
                    reason = "This URL has already been downloaded (use 'Override duplicate check' to re-download).";
                    break;
                default:
                    reason = "This URL is already in the system.";
                    break;
            }
            qDebug() << "DownloadManager: Skipping duplicate URL:" << url << "- Reason:" << reason;
            emit duplicateDownloadDetected(url, reason);
            return;
        }
    }

    // Intercept for download sections before anything else
    bool useSections = m_configManager->get("DownloadOptions", "download_sections_enabled", false).toBool();
    QString downloadTypeCheck = effectiveOptions.value("type", "video").toString();
    // The "download_sections_set" flag prevents an infinite loop.
    if (useSections && !isNonInteractiveRequest(effectiveOptions) && !effectiveOptions.contains("download_sections_set") && (downloadTypeCheck == "video" || downloadTypeCheck == "audio")) {
        qDebug() << "Download sections enabled, fetching metadata for" << url;
        fetchInfoForSections(url, effectiveOptions);
        return;
    }

    QString downloadType = effectiveOptions.value("type", "video").toString();

    // Intercept for runtime format selection before doing anything else
    bool needsRuntimeSelection = false;
    if (downloadType == "video") {
        if (m_configManager->get("Video", "video_quality").toString() == "Select at Runtime") {
            needsRuntimeSelection = true;
        }
    } else if (downloadType == "audio") {
        if (m_configManager->get("Audio", "audio_quality").toString() == "Select at Runtime") {
            needsRuntimeSelection = true;
        }
    }

    if (needsRuntimeSelection && !isNonInteractiveRequest(effectiveOptions) && !effectiveOptions.value("runtime_format_selected", false).toBool()) {
        fetchFormatsForSelection(url, effectiveOptions);
        return;
    }

    if (downloadType == "gallery") {
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
        uiData["id"] = item.id;
        uiData["url"] = url;
        uiData["status"] = "Checking for playlist...";
        uiData["options"] = effectiveOptions;
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

        QString playlistLogic = effectiveOptions.value("playlist_logic", "Ask").toString();
        expander->startExpansion(playlistLogic);
        emit playlistExpansionStarted(url);
    }
}

void DownloadManager::fetchInfoForSections(const QString &url, const QVariantMap &options)
{
    QProcess *process = new QProcess(this);
    QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", m_configManager).path;

    QStringList args;
    args << "--dump-json" << "--no-playlist" << url;

    QString cookiesBrowser = m_configManager->get("General", "cookies_from_browser", "None").toString();
    if (cookiesBrowser != "None") {
        args << "--cookies-from-browser" << cookiesBrowser.toLower();
    }

    connect(process, &QProcess::finished, this, [this, process, url, options](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            QByteArray output = process->readAllStandardOutput();
            QJsonDocument doc = QJsonDocument::fromJson(output);
            if (doc.isObject()) {
                QVariantMap infoJson = doc.object().toVariantMap();
                QMetaObject::invokeMethod(this, [this, url, options, infoJson]() {
                    emit downloadSectionsRequested(url, options, infoJson);
                }, Qt::QueuedConnection);
            } else {
                qWarning() << "Failed to parse JSON for sections, enqueuing without them.";
                QVariantMap newOptions = options;
                newOptions["download_sections_set"] = true; // Prevent re-triggering
                enqueueDownload(url, newOptions);
            }
        } else {
            qWarning() << "Failed to fetch info for sections, enqueuing without them. Error:" << process->readAllStandardError();
            QVariantMap newOptions = options;
            newOptions["download_sections_set"] = true; // Prevent re-triggering
            enqueueDownload(url, newOptions);
        }
        process->deleteLater();
    });

    process->start(ytDlpPath, args);
}

void DownloadManager::fetchFormatsForSelection(const QString &url, const QVariantMap &options) {
    QProcess *process = new QProcess(this);
    QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", m_configManager).path;
    
    QStringList args;
    args << "--dump-json" << "--no-playlist" << url;
    
    QString cookiesBrowser = m_configManager->get("General", "cookies_from_browser", "None").toString();
    if (cookiesBrowser != "None") {
        args << "--cookies-from-browser" << cookiesBrowser.toLower();
    }
    
    connect(process, &QProcess::finished, this, [this, process, url, options](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            QByteArray output = process->readAllStandardOutput();
            QJsonDocument doc = QJsonDocument::fromJson(output);
            if (doc.isObject()) {
                QVariantMap metadata = doc.object().toVariantMap();
                QVariantMap newOptions = options;
                if (metadata.value("is_live", false).toBool()) {
                    newOptions["is_live"] = true;
                }
                QMetaObject::invokeMethod(this, [this, url, newOptions, metadata]() {
                    emit formatSelectionRequested(url, newOptions, metadata);
                }, Qt::QueuedConnection);
            } else {
                const QString message = "yt-dlp returned invalid format metadata.";
                qWarning() << "DownloadManager: Invalid JSON returned from yt-dlp -J";
                QMetaObject::invokeMethod(this, [this, url, message]() {
                    emit formatSelectionFailed(url, message);
                }, Qt::QueuedConnection);
            }
        } else {
            const QString errorText = QString::fromUtf8(process->readAllStandardError()).trimmed();
            qWarning() << "DownloadManager: yt-dlp -J failed:" << errorText;
            QString message = errorText.isEmpty() ? "Failed to retrieve available formats." : errorText;
            QMetaObject::invokeMethod(this, [this, url, message]() {
                emit formatSelectionFailed(url, message);
            }, Qt::QueuedConnection);
        }
        process->deleteLater();
    });
    
    process->start(ytDlpPath, args);
}

void DownloadManager::resumeDownloadWithFormat(const QString &url, const QVariantMap &options, const QString &formatId) {
    QVariantMap newOptions = options;
    newOptions["runtime_format_selected"] = true;
    if (options.value("type", "video").toString() == "audio") {
        newOptions["runtime_audio_format"] = formatId;
    } else {
        newOptions["runtime_video_format"] = formatId;
    }
    enqueueDownload(url, newOptions);
}

