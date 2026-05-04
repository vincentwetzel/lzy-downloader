#include "MainWindow.h"
#include "MainWindowUiBuilder.h"
#include "StartTab.h"
#include "ActiveDownloadsTab.h"
#include "AdvancedSettingsTab.h"
#include "SortingTab.h"
#include "ToggleSwitch.h"
#include "ui/advanced_settings/BinariesPage.h"
#include "ui/MissingBinariesDialog.h"

#include "core/version.h"
#include "core/ConfigManager.h"
#include "core/DownloadManager.h"
#include "core/ProcessUtils.h"

#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QStyleFactory>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

void MainWindow::setupUI()
{
    setWindowTitle("LzyDownloader v" + QString(APP_VERSION_STRING));

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    m_startTab = new StartTab(m_configManager, m_extractorJsonParser, this);
    m_activeDownloadsTab = new ActiveDownloadsTab(m_configManager, this);
    m_advancedSettingsTab = new AdvancedSettingsTab(m_configManager, this);
    SortingTab *sortingTab = new SortingTab(m_configManager, this);

    m_uiBuilder->build(this, mainLayout, m_startTab, m_activeDownloadsTab, m_advancedSettingsTab, sortingTab);

    connect(m_uiBuilder->exitAfterSwitch(), &ToggleSwitch::toggled, this, [this](bool checked) {
        m_configManager->set("General", "exit_after", checked);
        m_configManager->save();
    });

    connect(m_startTab, &StartTab::downloadRequested, this, &MainWindow::onDownloadRequested);
    connect(m_startTab, &StartTab::navigateToExternalBinaries, this, [this]() {
        m_uiBuilder->tabWidget()->setCurrentWidget(m_advancedSettingsTab);
        m_advancedSettingsTab->navigateToCategory("External Binaries");
    });
    connect(m_startTab, &StartTab::missingBinariesDetected, this, [this](const QStringList &missingBinaries) {
        showMissingBinariesDialog(missingBinaries);
    });
    connect(m_advancedSettingsTab, &AdvancedSettingsTab::themeChanged, this, &MainWindow::applyTheme);

    connect(m_uiBuilder->tabWidget(), &QTabWidget::currentChanged, this, [this](int index) {
        if (m_uiBuilder->tabWidget()->widget(index) == m_startTab) {
            m_startTab->updateDynamicUI();
        }
    });
}

void MainWindow::setupTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(QIcon(":/app-icon"), this);
    m_trayIcon->setToolTip("LzyDownloader");

    m_trayMenu = new QMenu(this);
    QAction *showAction = m_trayMenu->addAction("Show");
    connect(showAction, &QAction::triggered, this, &QWidget::showNormal);
    QAction *quitAction = m_trayMenu->addAction("Quit");
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_configManager) {
        event->accept();
        return;
    }

    int activeCount = 0;
    int queuedCount = 0;

    QString activeText = m_uiBuilder->activeDownloadsLabel()->text();
    if (activeText.startsWith("Active: ")) {
        activeCount = activeText.mid(8).toInt();
    }

    QString queuedText = m_uiBuilder->queuedDownloadsLabel()->text();
    if (queuedText.startsWith("Queued: ")) {
        queuedCount = queuedText.mid(8).toInt();
    }

    if (activeCount > 0 || queuedCount > 0) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::warning(this, "Downloads in Progress",
                                     "There are downloads currently running or queued.\n\nAre you sure you want to exit?\nYour downloads will be safely saved and will resume the next time you start the application.",
                                     QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }
    }

    qInfo() << "Main window close requested; shutting down active background tasks before exit.";
    if (m_downloadManager) {
        m_downloadManager->shutdown();
    }

    const QList<QProcess*> remainingProcesses = findChildren<QProcess*>();
    for (QProcess *process : remainingProcesses) {
        if (process && process->state() == QProcess::Running) {
            process->disconnect();
            ProcessUtils::terminateProcessTree(process);
        }
    }

    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->hide();
    }
    event->accept();
    QApplication::quit();
}

bool MainWindow::event(QEvent *event)
{
    bool handled = QMainWindow::event(event);
    if (!m_configManager) {
        return handled;
    }

    int autoPasteMode = m_configManager->get("General", "auto_paste_mode", 0).toInt();

    if (event->type() == QEvent::WindowActivate || event->type() == QEvent::Enter) {
        if (autoPasteMode == 1) {
            handleClipboardAutoPaste(false);
        } else if (autoPasteMode == 3) {
            handleClipboardAutoPaste(true);
        }
    }
    return handled;
}

void MainWindow::onClipboardChanged()
{
    if (!m_configManager) {
        return;
    }

    int autoPasteMode = m_configManager->get("General", "auto_paste_mode", 0).toInt();

    if (autoPasteMode == 2) {
        handleClipboardAutoPaste(false);
    } else if (autoPasteMode == 4) {
        handleClipboardAutoPaste(true);
    }
}

void MainWindow::handleClipboardAutoPaste(bool forceEnqueue)
{
    if (!m_startTab || !m_uiBuilder || !m_uiBuilder->tabWidget() || !m_startTab->isEnabled()) {
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastAutoPasteTimestamp < 500) {
        return;
    }

    const QString clipboardText = m_clipboard->text().trimmed();
    if (clipboardText.isEmpty() || clipboardText == m_lastAutoPastedUrl) {
        return;
    }

    if (m_startTab->tryAutoPasteFromClipboard()) {
        m_lastAutoPastedUrl = clipboardText;
        m_lastAutoPasteTimestamp = QDateTime::currentMSecsSinceEpoch();

        if (m_uiBuilder->tabWidget()->currentWidget() != m_startTab) {
            m_uiBuilder->tabWidget()->setCurrentWidget(m_startTab);
        }
        m_startTab->focusUrlInput();

        if (forceEnqueue) {
            m_startTab->onDownloadButtonClicked();
        }
    }
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            activateWindow();
        }
    }
}

void MainWindow::onQueueFinished()
{
    if (!m_configManager) {
        return;
    }

    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage("Downloads Complete", "All queued media downloads have finished.",
                                QSystemTrayIcon::Information, 3000);
    }

    bool exitAfter = m_configManager->get("General", "exit_after", false).toBool();
    if (exitAfter) {
        qInfo() << "Queue finished and 'exit after' is enabled. Waiting 2 seconds before quitting to allow for final file cleanup.";
        QTimer::singleShot(2000, this, [this]() {
            if (!m_configManager || !m_uiBuilder) {
                return;
            }

            const bool exitStillEnabled = m_configManager->get("General", "exit_after", false).toBool();
            if (!exitStillEnabled) {
                qInfo() << "Exit-after timer cancelled because the setting was turned off before shutdown.";
                return;
            }

            int activeCount = 0;
            int queuedCount = 0;

            const QString activeText = m_uiBuilder->activeDownloadsLabel()->text();
            if (activeText.startsWith("Active: ")) {
                activeCount = activeText.mid(8).toInt();
            }

            const QString queuedText = m_uiBuilder->queuedDownloadsLabel()->text();
            if (queuedText.startsWith("Queued: ")) {
                queuedCount = queuedText.mid(8).toInt();
            }

            if (activeCount == 0 && queuedCount == 0) {
                QCoreApplication::quit();
                return;
            }

            qInfo() << "Exit-after timer cancelled because downloads resumed before shutdown."
                    << "Active:" << activeCount << "Queued:" << queuedCount;
        });
    }
}

void MainWindow::startStartupChecks()
{
    m_startupThread->start();
}

bool MainWindow::showMissingBinariesDialog(const QStringList &missingBinaries)
{
    if (missingBinaries.isEmpty()) {
        return true;
    }

    BinariesPage *binariesPage = m_advancedSettingsTab
        ? m_advancedSettingsTab->findChild<BinariesPage*>()
        : nullptr;

    MissingBinariesDialog dialog(missingBinaries, m_configManager, binariesPage, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    const bool resolved = dialog.allBinariesResolved();

    if (resolved) {
        ProcessUtils::clearCache();
        if (m_startTab) {
            m_startTab->updateDynamicUI();
        }
        return true;
    }

    if (accepted) {
        qWarning() << "Missing binary setup dialog accepted before all binaries resolved:"
                   << missingBinaries.join(", ");
    }
    return false;
}

void MainWindow::onVideoQualityWarning(const QString &url, const QString &message)
{
    if (m_nonInteractiveLaunch) {
        qWarning() << "Low quality video warning for non-interactive launch:" << url << message;
        return;
    }

    QMessageBox::warning(this, "Low Quality Video",
                         QString("The following video was downloaded at a low quality:\n%1\n\n%2").arg(url, message));
}

void MainWindow::applyTheme(const QString &themeName)
{
    qApp->setStyle(QStyleFactory::create("Fusion"));

    bool useDarkTheme = false;
    if (themeName == "Dark") {
        useDarkTheme = true;
    } else if (themeName == "System") {
        const auto colorScheme = qApp->styleHints()->colorScheme();
        useDarkTheme = (colorScheme == Qt::ColorScheme::Dark);
    }

    if (useDarkTheme) {
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
        darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        darkPalette.setColor(QPalette::Mid, QColor(40, 40, 40));
        qApp->setPalette(darkPalette);
    } else {
        QPalette lightPalette(QColor(240, 240, 240));
        lightPalette.setColor(QPalette::WindowText, Qt::black);
        lightPalette.setColor(QPalette::Base, Qt::white);
        lightPalette.setColor(QPalette::AlternateBase, QColor(246, 246, 246));
        lightPalette.setColor(QPalette::ToolTipBase, Qt::white);
        lightPalette.setColor(QPalette::ToolTipText, Qt::black);
        lightPalette.setColor(QPalette::Text, Qt::black);
        lightPalette.setColor(QPalette::ButtonText, Qt::black);
        lightPalette.setColor(QPalette::BrightText, Qt::red);
        lightPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        lightPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        lightPalette.setColor(QPalette::HighlightedText, Qt::white);
        qApp->setPalette(lightPalette);
    }
}

void MainWindow::updateTotalSpeed(double speed)
{
    double speedMb = speed / (1024.0 * 1024.0);
    m_uiBuilder->speedLabel()->setText(QString("Current Speed: %1 MB/s").arg(speedMb, 0, 'f', 2));
}

void MainWindow::onDownloadStatsUpdated(int queued, int active, int completed, int errors)
{
    m_uiBuilder->queuedDownloadsLabel()->setText(QString("Queued: %1").arg(queued));
    m_uiBuilder->activeDownloadsLabel()->setText(QString("Active: %1").arg(active));
    m_uiBuilder->completedDownloadsLabel()->setText(QString("Completed: %1").arg(completed));
    m_uiBuilder->errorDownloadsLabel()->setText(QString("Errors: %1").arg(errors));
}

void MainWindow::setYtDlpVersion(const QString &version)
{
    Q_UNUSED(version);
}
