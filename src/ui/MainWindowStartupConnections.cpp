#include "MainWindow.h"
#include "AdvancedSettingsTab.h"
#include "StartTab.h"
#include "core/ConfigManager.h"
#include "core/StartupWorker.h"
#include "ui/advanced_settings/BinariesPage.h"

#include <QClipboard>
#include <QStatusBar>
#include <QThread>

void MainWindow::connectStartupWorkerSignals()
{
    m_startupWorker->moveToThread(m_startupThread);
    connect(m_startupThread, &QThread::started, m_startupWorker, &StartupWorker::start);
    connect(m_startupWorker, &StartupWorker::finished, m_startupThread, &QThread::quit);
    connect(m_startupThread, &QThread::finished, m_startupWorker, &QObject::deleteLater);
    connect(m_startupWorker, &StartupWorker::binariesChecked, this, [this](const QStringList &missingBinaries) {
        m_startupMissingBinaries = missingBinaries;
        if (m_startTab) {
            m_startTab->setStartupBinaryStatus(missingBinaries);
        }
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
        if (m_appUpdateCheckPending || m_appUpdatePromptActive || m_appUpdateInstalling) {
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
        if (m_startTab) {
            m_startTab->setStartupChecksFinished(true);
        }
        showStartupBinarySetupIfReady();
    });
    connect(m_startupWorker, &StartupWorker::ytDlpVersionFetched, this, &MainWindow::setYtDlpVersion);
    connect(m_startupWorker, &StartupWorker::galleryDlVersionFetched, this, [this](const QString &version) {
        m_galleryDlVersion = version;
        if (m_advancedSettingsTab) {
            m_advancedSettingsTab->setGalleryDlVersion(version);
        }
    });
    connect(m_clipboard, &QClipboard::changed, this, &MainWindow::onClipboardChanged);
}
