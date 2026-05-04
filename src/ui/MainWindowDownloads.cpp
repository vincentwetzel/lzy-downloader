#include "MainWindow.h"
#include "MainWindowHelpers.h"
#include "MainWindowUiBuilder.h"
#include "ActiveDownloadsTab.h"
#include "RuntimeSelectionDialog.h"
#include "DownloadSectionsDialog.h"

#include "core/version.h"
#include "core/ArchiveManager.h"
#include "core/ConfigManager.h"
#include "core/DownloadManager.h"
#include "core/ProcessUtils.h"
#include "core/UrlValidator.h"
#include "core/download_pipeline/YtDlpDownloadInfoExtractor.h"

#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTabWidget>

QString MainWindow::appVersion() const
{
    return QString(APP_VERSION_STRING);
}

void MainWindow::onLocalApiEnqueueRequested(const QString &url, const QString &type, const QString &jobId)
{
    QVariantMap options;
    options["type"] = type.isEmpty() ? "video" : type;
    if (!jobId.isEmpty()) {
        options["id"] = jobId;
    }
    MainWindowHelpers::applyNonInteractiveDownloadDefaults(options);

    onDownloadRequested(url, options);
}

void MainWindow::onDownloadRequested(const QString &url, const QVariantMap &options)
{
    const bool nonInteractive = MainWindowHelpers::isNonInteractiveRequest(options);

    if (!m_pendingUrl.isEmpty()) {
        if (nonInteractive) {
            qWarning() << "Ignoring non-interactive download request while another metadata request is pending:" << url;
        } else {
            QMessageBox::warning(this, "Please Wait", "Currently fetching info for another download.");
        }
        return;
    }

    QString type = options.value("type").toString();
    QStringList missingBinaries;

    if (type == "gallery") {
        QStringList required = {"gallery-dl", "ffmpeg", "ffprobe"};
        for (const QString &bin : required) {
            QString source = ProcessUtils::findBinary(bin, m_configManager).source;
            if (source == "Not Found" || source == "Invalid Custom") {
                missingBinaries << bin;
            }
        }
    } else if (type == "view_formats") {
        QString source = ProcessUtils::findBinary("yt-dlp", m_configManager).source;
        if (source == "Not Found" || source == "Invalid Custom") {
            missingBinaries << "yt-dlp";
        }
    } else {
        QStringList required = {"yt-dlp", "ffmpeg", "ffprobe", "deno"};
        for (const QString &bin : required) {
            QString source = ProcessUtils::findBinary(bin, m_configManager).source;
            if (source == "Not Found" || source == "Invalid Custom") {
                missingBinaries << bin;
            }
        }

        bool useAria2c = m_configManager->get("Metadata", "use_aria2c", false).toBool();
        if (useAria2c) {
            QString ariaSource = ProcessUtils::findBinary("aria2c", m_configManager).source;
            if (ariaSource == "Not Found" || ariaSource == "Invalid Custom") {
                qWarning() << "aria2c is enabled but missing. Auto-reverting to yt-dlp native downloader.";
                m_configManager->set("Metadata", "use_aria2c", false);
                m_configManager->save();
            }
        }
    }

    if (!missingBinaries.isEmpty()) {
        if (nonInteractive) {
            qWarning() << "Cannot queue non-interactive download because required binaries are missing:"
                       << missingBinaries.join(", ") << "URL:" << url;
            return;
        }

        if (!showMissingBinariesDialog(missingBinaries)) {
            return;
        }

        missingBinaries.clear();
        const QStringList requiredAfterSetup = type == "gallery"
            ? QStringList{"gallery-dl", "ffmpeg", "ffprobe"}
            : type == "view_formats"
                ? QStringList{"yt-dlp"}
                : QStringList{"yt-dlp", "ffmpeg", "ffprobe", "deno"};
        for (const QString &bin : requiredAfterSetup) {
            QString source = ProcessUtils::resolveBinary(bin, m_configManager).source;
            if (source == "Not Found" || source == "Invalid Custom") {
                missingBinaries << bin;
            }
        }
        if (!missingBinaries.isEmpty()) {
            return;
        }
    }

    QVariantMap mutableOptions = options;
    if (nonInteractive) {
        MainWindowHelpers::applyNonInteractiveDownloadDefaults(mutableOptions);
    }

    bool overrideArchive = mutableOptions.value("override_archive", m_configManager->get("General", "override_archive", false)).toBool();

    if (!overrideArchive && m_archiveManager && m_archiveManager->isInArchive(url)) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Duplicate Download",
                                      QString("The following URL is already in your download history:\n%1\n\nDo you want to download it again?").arg(url),
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            return;
        }
        mutableOptions["override_archive"] = true;
    }

    bool runtimeSubs = m_configManager->get("Subtitles", "languages", "en").toString().split(',').contains("runtime");

    if (runtimeSubs && !nonInteractive) {
        m_pendingUrl = url;
        m_pendingOptions = mutableOptions;
        statusBar()->showMessage("Fetching media info for runtime selection...");
        QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", m_configManager).path;
        QStringList args;
        args << "--dump-json" << "--no-playlist" << url;
        m_runtimeExtractor->extract(ytDlpPath, args);
        return;
    }

    static QRegularExpression fastTrackRe(R"(^(https?://)?(www\.)?(youtube\.com|youtu\.be|music\.youtube\.com|tiktok\.com|instagram\.com|twitter\.com|x\.com)/)");
    if (fastTrackRe.match(url).hasMatch()) {
        m_downloadManager->enqueueDownload(url, mutableOptions);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
        return;
    }

    m_pendingUrl = url;
    m_pendingOptions = mutableOptions;

    m_urlValidator->validate(url);
}

void MainWindow::onRuntimeInfoReady(const QVariantMap &info)
{
    statusBar()->clearMessage();
    bool runtimeSubs = m_configManager->get("Subtitles", "languages", "en").toString().split(',').contains("runtime");

    RuntimeSelectionDialog dialog(info, false, false, runtimeSubs, this);
    if (dialog.exec() == QDialog::Accepted) {
        QVariantMap opts = m_pendingOptions;
        if (runtimeSubs) {
            QStringList subs = dialog.getSelectedSubtitles();
            if (!subs.isEmpty()) opts["runtime_subtitles"] = subs.join(',');
        }
        m_downloadManager->enqueueDownload(m_pendingUrl, opts);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
    }
    m_pendingUrl.clear();
    m_pendingOptions.clear();
}

void MainWindow::onRuntimeInfoError(const QString &error)
{
    statusBar()->clearMessage();
    if (MainWindowHelpers::isNonInteractiveRequest(m_pendingOptions)) {
        qWarning() << "Runtime metadata extraction failed for non-interactive request:" << error;
    } else {
        QMessageBox::warning(this, "Extraction Error", "Failed to fetch media info for runtime selection:\n" + error);
    }
    m_pendingUrl.clear();
    m_pendingOptions.clear();
}

void MainWindow::onDownloadSectionsRequested(const QString &url, const QVariantMap &options, const QVariantMap &infoJson)
{
    if (MainWindowHelpers::isNonInteractiveRequest(options)) {
        QVariantMap newOptions = options;
        newOptions["download_sections_set"] = true;
        qInfo() << "Skipping download sections dialog for non-interactive request:" << url;
        m_downloadManager->enqueueDownload(url, newOptions);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
        return;
    }

    DownloadSectionsDialog dialog(infoJson, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString sections = dialog.getSectionsString();
        QString sectionLabel = dialog.getFilenameLabel();
        QVariantMap newOptions = options;
        newOptions["download_sections_set"] = true;
        if (!sections.isEmpty()) {
            newOptions["download_sections"] = sections;
        }
        if (!sectionLabel.isEmpty()) {
            newOptions["download_sections_label"] = sectionLabel;
        }
        m_downloadManager->enqueueDownload(url, newOptions);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
    } else {
        qInfo() << "Download sections selection cancelled by user for" << url;
    }
}

void MainWindow::onValidationFinished(bool isValid, const QString &error)
{
    if (isValid) {
        m_downloadManager->enqueueDownload(m_pendingUrl, m_pendingOptions);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
    } else {
        if (MainWindowHelpers::isNonInteractiveRequest(m_pendingOptions)) {
            qWarning() << "Non-interactive URL validation failed for" << m_pendingUrl << ":" << error;
        } else {
            QMessageBox::warning(this, "Invalid URL", "The URL could not be validated:\n" + error);
        }
    }
    m_pendingUrl.clear();
    m_pendingOptions.clear();
}

void MainWindow::onYtDlpErrorPopup(const QString &id, const QString &errorType, const QString &userMessage, const QString &rawError, const QVariantMap &itemData)
{
    Q_UNUSED(id);

    QString url = itemData.value("url").toString();
    QVariantMap requestOptions = itemData.value("options").toMap();
    const bool nonInteractive = MainWindowHelpers::isNonInteractiveRequest(requestOptions);
    QString urlHtml = url.isEmpty() ? "" : QString("<br><br><a href=\"%1\">%1</a>").arg(url.toHtmlEscaped());
    QString richUserMessage = userMessage.toHtmlEscaped().replace("\n", "<br>");

    QString cleanError = rawError;
    if (cleanError.startsWith("ERROR: ")) {
        cleanError = cleanError.mid(7);
    }
    cleanError.remove(QRegularExpression("^\\[[^\\]]+\\]\\s*"));

    if (errorType == "scheduled_livestream") {
        if (nonInteractive) {
            QVariantMap newItemData = itemData;
            QVariantMap options = requestOptions;
            options["wait_for_video"] = true;
            options["livestream_wait_min"] = m_configManager->get("Livestream", "wait_for_video_min", 60).toInt();
            options["livestream_wait_max"] = m_configManager->get("Livestream", "wait_for_video_max", 300).toInt();
            MainWindowHelpers::applyNonInteractiveDownloadDefaults(options);
            newItemData["options"] = options;
            qInfo() << "Automatically waiting for scheduled livestream in non-interactive request:" << url;
            m_downloadManager->restartDownloadWithOptions(newItemData);
            return;
        }

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Scheduled Livestream");
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        msgBox.setText(richUserMessage + urlHtml);
        if (!cleanError.isEmpty()) {
            msgBox.setInformativeText(cleanError.toHtmlEscaped().replace("\n", "<br>"));
        }
        msgBox.setIcon(QMessageBox::Information);

        QPushButton *waitButton = msgBox.addButton("Wait and Download When Available", QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Cancel);

        msgBox.exec();

        if (msgBox.clickedButton() == waitButton) {
            QVariantMap newItemData = itemData;
            QVariantMap options = newItemData["options"].toMap();
            options["wait_for_video"] = true;

            int minWait;
            int maxWait;
            if (cleanError.contains("days", Qt::CaseInsensitive) || cleanError.contains("hours", Qt::CaseInsensitive)) {
                minWait = 1800;
                maxWait = 3600;
            } else if (cleanError.contains("minutes", Qt::CaseInsensitive)) {
                minWait = 60;
                maxWait = 300;
            } else {
                minWait = m_configManager->get("Livestream", "wait_for_video_min", 60).toInt();
                maxWait = m_configManager->get("Livestream", "wait_for_video_max", 300).toInt();
            }

            options["livestream_wait_min"] = minWait;
            options["livestream_wait_max"] = maxWait;
            newItemData["options"] = options;
            m_downloadManager->restartDownloadWithOptions(newItemData);
        }
    } else {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Download Error");
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        msgBox.setText(richUserMessage + urlHtml);
        if (!cleanError.isEmpty()) {
            msgBox.setInformativeText(cleanError.toHtmlEscaped().replace("\n", "<br>"));
        }
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.exec();
    }
}
