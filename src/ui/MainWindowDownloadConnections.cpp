#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MainWindowUiBuilder.h"
#include "StartTab.h"
#include "ActiveDownloadsTab.h"
#include "AdvancedSettingsTab.h"
#include "DownloadHistoryTab.h"
#include "FormatSelectionDialog.h"
#include "ui/advanced_settings/BinariesPage.h"

#include "core/version.h"
#include "core/PlaylistRangeDialog.h"
#include "core/AppUpdater.h"
#include "core/ConfigManager.h"
#include "core/DownloadManager.h"
#include "core/LocalApiServer.h"
#include "core/StartupWorker.h"
#include "core/UrlValidator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <chrono>

void MainWindow::connectDownloadManagerSignals()
{
    // CRITICAL: Explicitly flush state and tear down workers before quitting!
    // This compensates for QCoreApplication::quit() bypassing MainWindow::closeEvent
    // during automated headless shutdowns (like --server --exit-after).
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_downloadManager) {
            qInfo() << "Executing headless shutdown/cleanup sequence before event loop terminates...";
            m_downloadManager->shutdown();
        }
    });

    // Download History tracking
    QSharedPointer<QMap<QString, HistoryItemData>> historyStates = QSharedPointer<QMap<QString, HistoryItemData>>::create();

    auto cacheThumbnail = [this](const QString &id, const QString &originalPath) -> QString {
        if (originalPath.isEmpty() || originalPath.startsWith(QStringLiteral("http://")) || originalPath.startsWith(QStringLiteral("https://"))) {
            return originalPath;
        }
        QString cacheDir = QDir(m_configManager->getConfigDir()).filePath(QStringLiteral("thumbnails"));
        QDir().mkpath(cacheDir);
        QString cachedPath = QDir(cacheDir).filePath(QStringLiteral("%1_%2").arg(id, QFileInfo(originalPath).fileName()));
        if (!QFile::exists(cachedPath) && QFile::exists(originalPath)) {
            QFile::copy(originalPath, cachedPath);
        }
        return QFile::exists(cachedPath) ? cachedPath : originalPath;
    };

    connect(m_downloadManager, &DownloadManager::downloadAddedToQueue, this, [this, historyStates, cacheThumbnail](const QVariantMap &itemData) {
        const QString id = itemData.value(QStringLiteral("id")).toString();
        HistoryItemData data;
        data.id = id;
        data.url = itemData.value(QStringLiteral("url")).toString();
        data.title = itemData.value(QStringLiteral("title")).toString();
        data.timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm AP"));

        if (itemData.contains(QStringLiteral("duration"))) {
            const int durationSecs = itemData.value(QStringLiteral("duration")).toInt();
            if (durationSecs > 0) {
                const int hours = durationSecs / 3600;
                const int minutes = (durationSecs % 3600) / 60;
                const int seconds = durationSecs % 60;
                if (hours > 0) {
                    data.duration = QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
                } else {
                    data.duration = QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
                }
            }
        } else if (itemData.contains(QStringLiteral("duration_string"))) {
            const QString durStr = itemData.value(QStringLiteral("duration_string")).toString();
            if (!durStr.isEmpty() && !durStr.contains(QLatin1Char(':'))) {
                data.duration = QStringLiteral("0:%1").arg(durStr.toInt(), 2, 10, QLatin1Char('0'));
            } else {
                data.duration = durStr;
            }
        }

        QVariantMap updatedItemData = itemData;
        if (itemData.contains(QStringLiteral("thumbnail_path"))) {
            const QString originalPath = itemData.value(QStringLiteral("thumbnail_path")).toString();
            const QString finalPath = cacheThumbnail(id, originalPath);
            data.thumbnailPath = finalPath;
            updatedItemData.insert(QStringLiteral("thumbnail_path"), finalPath);
        }

        (*historyStates)[id] = data;
        m_activeDownloadsTab->addDownloadItem(updatedItemData);
    });

    connect(m_downloadManager, &DownloadManager::downloadProgress, this, [this, historyStates, cacheThumbnail](const QString &id, const QVariantMap &progressData) {
        QVariantMap updatedProgress = progressData;
        if (historyStates->contains(id)) {
            if (progressData.contains(QStringLiteral("title"))) {
                (*historyStates)[id].title = progressData.value(QStringLiteral("title")).toString().trimmed();
            }
            if (progressData.contains(QStringLiteral("thumbnail_path"))) {
            const QString originalPath = progressData.value(QStringLiteral("thumbnail_path")).toString();
            const QString finalPath = cacheThumbnail(id, originalPath);
                (*historyStates)[id].thumbnailPath = finalPath;
            updatedProgress.insert(QStringLiteral("thumbnail_path"), finalPath);
            }
            if (progressData.contains(QStringLiteral("total_bytes"))) {
                (*historyStates)[id].totalBytes = progressData.value(QStringLiteral("total_bytes")).toLongLong();
            } else if (progressData.contains(QStringLiteral("total_size")) && (*historyStates)[id].totalBytes == 0) {
                QString sizeStr = progressData.value(QStringLiteral("total_size")).toString();
                sizeStr.remove(QLatin1Char('~'));
                if (sizeStr.endsWith(QStringLiteral("MiB"))) {
                    (*historyStates)[id].totalBytes = static_cast<qint64>(sizeStr.remove(QStringLiteral("MiB")).trimmed().toDouble() * 1024 * 1024);
                } else if (sizeStr.endsWith(QStringLiteral("KiB"))) {
                    (*historyStates)[id].totalBytes = static_cast<qint64>(sizeStr.remove(QStringLiteral("KiB")).trimmed().toDouble() * 1024);
                } else if (sizeStr.endsWith(QStringLiteral("GiB"))) {
                    (*historyStates)[id].totalBytes = static_cast<qint64>(sizeStr.remove(QStringLiteral("GiB")).trimmed().toDouble() * 1024 * 1024 * 1024);
                }
            }
            if (progressData.contains(QStringLiteral("duration")) && (*historyStates)[id].duration.isEmpty()) {
            const int durationSecs = progressData.value(QStringLiteral("duration")).toInt();
                if (durationSecs > 0) {
                const int hours = durationSecs / 3600;
                const int minutes = (durationSecs % 3600) / 60;
                const int seconds = durationSecs % 60;
                    if (hours > 0) {
                        (*historyStates)[id].duration = QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
                    } else {
                        (*historyStates)[id].duration = QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
                    }
                }
            } else if (progressData.contains(QStringLiteral("duration_string")) && (*historyStates)[id].duration.isEmpty()) {
            const QString durStr = progressData.value(QStringLiteral("duration_string")).toString();
                if (!durStr.isEmpty() && !durStr.contains(QLatin1Char(':'))) {
                    (*historyStates)[id].duration = QStringLiteral("0:%1").arg(durStr.toInt(), 2, 10, QLatin1Char('0'));
                } else {
                    (*historyStates)[id].duration = durStr;
                }
            }
        }
        m_activeDownloadsTab->updateDownloadProgress(id, updatedProgress);
    });

    connect(m_downloadManager, &DownloadManager::downloadFinalPathReady, this, [historyStates](const QString &id, const QString &path) {
        if (historyStates->contains(id)) {
            (*historyStates)[id].filePath = path;
        }
    });

    connect(m_downloadManager, &DownloadManager::downloadFinished, this, [this, historyStates](const QString &id, bool success, const QString &message) {
        Q_UNUSED(message);
        if (success && historyStates->contains(id)) {
            if (auto *historyTab = findChild<DownloadHistoryTab*>(QStringLiteral("downloadHistoryTab"))) {
                historyTab->addHistoryItem((*historyStates)[id]);
            }
        }
        historyStates->remove(id);
    });
    
    connect(m_downloadManager, &DownloadManager::downloadCancelled, this, [historyStates](const QString &id) {
        historyStates->remove(id);
    });

    connect(m_downloadManager, &DownloadManager::downloadFinished,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadFinished);
    connect(m_downloadManager, &DownloadManager::downloadCancelled,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadCancelled);
    connect(m_downloadManager, &DownloadManager::downloadPaused,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadPaused);
    connect(m_downloadManager, &DownloadManager::downloadResumed,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadResumed);
    connect(m_downloadManager, &DownloadManager::downloadFinalPathReady,
            m_activeDownloadsTab, &ActiveDownloadsTab::onDownloadFinalPathReady);
    connect(m_downloadManager, &DownloadManager::downloadRemovedFromQueue,
            m_activeDownloadsTab, &ActiveDownloadsTab::removeDownloadItem);
    connect(m_downloadManager, &DownloadManager::playlistExpansionStarted,
            m_activeDownloadsTab, &ActiveDownloadsTab::addExpandingPlaylist);
    connect(m_downloadManager, &DownloadManager::playlistExpansionFinished,
            m_activeDownloadsTab, &ActiveDownloadsTab::removeExpandingPlaylist);
    connect(m_downloadManager, &DownloadManager::queueFinished, this, &MainWindow::onQueueFinished);
    connect(m_downloadManager, &DownloadManager::totalSpeedUpdated, this, &MainWindow::updateTotalSpeed);
    connect(m_downloadManager, &DownloadManager::videoQualityWarning, this, &MainWindow::onVideoQualityWarning);
    connect(m_downloadManager, &DownloadManager::downloadStatsUpdated, this, &MainWindow::onDownloadStatsUpdated);

    connect(m_downloadManager, &DownloadManager::downloadAddedToQueue, m_localApiServer, &LocalApiServer::onDownloadAdded);
    connect(m_downloadManager, &DownloadManager::downloadProgress, m_localApiServer, &LocalApiServer::onDownloadProgress);
    connect(m_downloadManager, &DownloadManager::downloadFinished, m_localApiServer, &LocalApiServer::onDownloadFinished);
    connect(m_downloadManager, &DownloadManager::downloadCancelled, m_localApiServer, &LocalApiServer::onDownloadCancelled);
    connect(m_downloadManager, &DownloadManager::downloadRemovedFromQueue, m_localApiServer, &LocalApiServer::onDownloadRemoved);
    connect(m_downloadManager, &DownloadManager::nonInteractiveRequestFailed,
            m_localApiServer, &LocalApiServer::onNonInteractiveRequestFailed);

    connectDiscordWebhookSignals();

    // Non-interactive/API requests report duplicate failures through the
    // webhook signal.  Do not route the same event through the GUI warning
    // slot, especially when the API is hosted by an already-open GUI process.
    connect(m_downloadManager, &DownloadManager::duplicateDownloadDetected, this,
            [this](const QString &url, const QString &reason) {
                if (!m_nonInteractiveLaunch) {
                    m_startTab->onDuplicateDownloadDetected(url, reason);
                }
            });
    connect(m_downloadManager, &DownloadManager::nonInteractiveRequestFailed,
            this, &MainWindow::nonInteractiveRequestFailed);
    connect(m_downloadManager, &DownloadManager::ytDlpErrorPopupRequested, this, &MainWindow::onYtDlpErrorPopup);
    connect(m_downloadManager, &DownloadManager::downloadSectionsRequested, this, &MainWindow::onDownloadSectionsRequested);

    connect(m_downloadManager, &DownloadManager::playlistActionRequested, this,
            [this](const QString &url, int itemCount, const QVariantMap &options, const QList<QVariantMap> &expandedItems) {
                if (m_nonInteractiveLaunch || MainWindowHelpers::isNonInteractiveRequest(options)) {
                    qInfo() << "Non-interactive playlist request detected; queueing all playlist items for" << url << "count:" << itemCount;
                    m_downloadManager->processPlaylistSelection(url, QStringLiteral("Download All"), options, expandedItems);
                    m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
                    return;
                }

                QMessageBox msgBox(this);
                msgBox.setIcon(QMessageBox::Question);
                msgBox.setWindowTitle(tr("Playlist Detected"));
                msgBox.setText(tr("This URL contains a playlist with %1 item(s).").arg(itemCount));
                msgBox.setInformativeText(tr("Do you want to queue every item or just the first one?"));

                QPushButton *downloadAllButton = msgBox.addButton(tr("Download All"), QMessageBox::AcceptRole);
                QPushButton *downloadPartButton = msgBox.addButton(tr("Download Part..."), QMessageBox::ActionRole);
                QPushButton *downloadSingleButton = msgBox.addButton(tr("Download Single Item"), QMessageBox::ActionRole);
                QPushButton *cancelButton = msgBox.addButton(QMessageBox::Cancel);

                msgBox.exec();

                QString action;
                QList<QVariantMap> itemsToProcess = expandedItems;

                if (msgBox.clickedButton() == downloadAllButton) {
                    action = QStringLiteral("Download All");
                } else if (msgBox.clickedButton() == downloadPartButton) {
                    PlaylistRangeDialog rangeDialog(expandedItems, this);
                    if (rangeDialog.exec() == QDialog::Accepted) {
                        itemsToProcess = rangeDialog.getSelectedItems();
                        if (!itemsToProcess.isEmpty()) {
                            action = QStringLiteral("Download Part");
                        } else {
                            action = QStringLiteral("Cancel");
                        }
                    } else {
                        action = QStringLiteral("Cancel");
                    }
                } else if (msgBox.clickedButton() == downloadSingleButton) {
                    action = QStringLiteral("Download Single Item");
                } else if (msgBox.clickedButton() == cancelButton) {
                    action = QStringLiteral("Cancel");
                }

                m_downloadManager->processPlaylistSelection(url, action, options, itemsToProcess);
                m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
            });

    connect(m_downloadManager, &DownloadManager::formatSelectionRequested, this,
            [this](const QString &url, const QVariantMap &options, const QVariantMap &infoDict) {
                if (m_nonInteractiveLaunch || MainWindowHelpers::isNonInteractiveRequest(options)) {
                    QVariantMap newOptions = options;
                newOptions.insert(QStringLiteral("runtime_format_selected"), true);
                    qInfo() << "Skipping runtime format dialog for non-interactive request:" << url;
                    m_downloadManager->enqueueDownload(url, newOptions);
                    return;
                }

                FormatSelectionDialog dialog(infoDict, options, this);
                if (dialog.exec() == QDialog::Accepted) {
                    QStringList selectedFormats = dialog.getSelectedFormatIds();
                    if (!selectedFormats.isEmpty()) {
                        for (const QString &formatId : selectedFormats) {
                            QVariantMap newOptions = options;
                        newOptions.insert(QStringLiteral("runtime_format_selected"), true);
                        newOptions.insert(QStringLiteral("format"), formatId);
                            m_downloadManager->enqueueDownload(url, newOptions);
                        }
                        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
                    }
                }
            });

    connect(m_activeDownloadsTab, &ActiveDownloadsTab::cancelDownloadRequested,
            m_downloadManager, &DownloadManager::cancelDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::retryDownloadRequested,
            m_downloadManager, &DownloadManager::retryDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::resumeDownloadRequested,
            m_downloadManager, &DownloadManager::resumeDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::pauseDownloadRequested,
            m_downloadManager, &DownloadManager::pauseDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::unpauseDownloadRequested,
            m_downloadManager, &DownloadManager::unpauseDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::moveDownloadUpRequested,
            m_downloadManager, &DownloadManager::moveDownloadUp);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::moveDownloadDownRequested,
            m_downloadManager, &DownloadManager::moveDownloadDown);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::finishDownloadRequested,
            m_downloadManager, &DownloadManager::finishDownload);
    connect(m_activeDownloadsTab, &ActiveDownloadsTab::itemCleared,
            m_downloadManager, &DownloadManager::onItemCleared);

    connect(m_urlValidator, &UrlValidator::validationFinished, this, &MainWindow::onValidationFinished);
}

void MainWindow::connectStartupWorkerSignals()
{
    m_startupWorker->moveToThread(m_startupThread);
    connect(m_startupThread, &QThread::started, m_startupWorker, &StartupWorker::start);
    connect(m_startupWorker, &StartupWorker::finished, m_startupThread, &QThread::quit);
    connect(m_startupThread, &QThread::finished, m_startupWorker, &QObject::deleteLater);
    connect(m_startupWorker, &StartupWorker::binariesChecked, this, [this](const QStringList &missingBinaries) {
        m_startupMissingBinaries = missingBinaries;
    });

    // Collect startup binary notices and show one consolidated setup window
    // after all checks complete. This avoids stacking one modal per tool.
    connect(m_startupWorker, &StartupWorker::binaryUpdateRequired, this, [this](const QString &binaryName, const QString &details) {
        if (m_nonInteractiveLaunch) return;

        if (m_advancedSettingsTab) {
            if (auto *binariesPage = m_advancedSettingsTab->findChild<BinariesPage*>()) {
                binariesPage->setBinaryWarning(binaryName, details);
            }
        }

        // Give the application update decision priority over binary updates.
        // Keep the warning, but defer automatic replacement and the checklist
        // until the application update check has resolved.
        if (m_appUpdateCheckPending || m_appUpdateInstalling) {
            const bool updateAvailable = m_configManager->get(
                QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), false).toBool();
            if (updateAvailable) {
                m_startupUpdateDetails.insert(binaryName, details);
            }
            return;
        }

        bool automaticUpdateStarted = false;
        if (m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("setup_completed"), false).toBool() &&
            m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), false).toBool() &&
            m_advancedSettingsTab) {
            if (auto *binariesPage = m_advancedSettingsTab->findChild<BinariesPage*>()) {
                automaticUpdateStarted = binariesPage->tryAutomaticUpdate(binaryName);
            }
        }
        statusBar()->showMessage(automaticUpdateStarted
                                      ? tr("Updating app-managed %1 in the background.").arg(binaryName)
                                      : tr("%1: %2").arg(binaryName, details),
                                  8000);

        const bool updateAvailable = m_configManager->get(
            QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), false).toBool();
        if (!automaticUpdateStarted && updateAvailable) {
            m_startupUpdateDetails.insert(binaryName, details);
        }
    });
    connect(m_startupWorker, &StartupWorker::finished, this, [this]() {
        if (m_nonInteractiveLaunch) {
            return;
        }

        m_startupChecksFinished = true;
        showStartupBinarySetupIfReady();
    });
    connect(m_startupWorker, &StartupWorker::ytDlpVersionFetched, this, &MainWindow::setYtDlpVersion);
    connect(m_startupWorker, &StartupWorker::galleryDlVersionFetched,
            m_advancedSettingsTab, &AdvancedSettingsTab::setGalleryDlVersion);
    connect(m_clipboard, &QClipboard::changed, this, &MainWindow::onClipboardChanged);
}

void MainWindow::releaseDeferredStartupBinaryUpdates()
{
    if (m_appUpdateInstalling || m_startupUpdateDetails.isEmpty() || !m_advancedSettingsTab) {
        return;
    }

    auto *binariesPage = m_advancedSettingsTab->findChild<BinariesPage*>();
    if (!binariesPage) {
        return;
    }

    for (auto it = m_startupUpdateDetails.begin(); it != m_startupUpdateDetails.end();) {
        const QString binaryName = it.key();
        bool started = false;
        if (m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("setup_completed"), false).toBool() &&
            m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), false).toBool()) {
            started = binariesPage->tryAutomaticUpdate(binaryName);
        }
        if (started) {
            statusBar()->showMessage(tr("Updating app-managed %1 in the background.").arg(binaryName), 8000);
            it = m_startupUpdateDetails.erase(it);
        } else {
            ++it;
        }
    }
}

void MainWindow::showStartupBinarySetupIfReady()
{
    if (m_nonInteractiveLaunch || m_appUpdateCheckPending || m_appUpdateInstalling ||
        !m_startupChecksFinished || m_startupSetupPresented) {
        return;
    }

    m_startupSetupPresented = true;

    QStringList attention = m_startupMissingBinaries;
    for (auto it = m_startupUpdateDetails.cbegin(); it != m_startupUpdateDetails.cend(); ++it) {
        if (!attention.contains(it.key())) {
            attention.append(it.key());
        }
    }
    if (!attention.isEmpty()) {
        showMissingBinariesDialog(attention, m_startupUpdateDetails);
    } else if (!m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("setup_completed"), false).toBool()) {
        m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("setup_completed"), true);
        m_configManager->save();
    }
}

void MainWindow::queueDirectCliDownload()
{
    const QString cliUrl = MainWindowHelpers::directCliUrl();
    if (!cliUrl.isEmpty()) {
        QTimer::singleShot(std::chrono::milliseconds(500), this, [this, cliUrl]() {
            QVariantMap options;
            if (QCoreApplication::arguments().contains(QStringLiteral("--audio"))) {
                options.insert(QStringLiteral("type"), QStringLiteral("audio"));
            } else {
                options.insert(QStringLiteral("type"), QStringLiteral("video"));
            }
            MainWindowHelpers::applyNonInteractiveDownloadDefaults(options);
            onDownloadRequested(cliUrl, options);
        });
    }
}


