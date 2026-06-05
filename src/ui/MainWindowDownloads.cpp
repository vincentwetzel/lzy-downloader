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
    options.insert(QStringLiteral("type"), type.isEmpty() ? QStringLiteral("video") : type);
    if (!jobId.isEmpty()) {
        options.insert(QStringLiteral("id"), jobId);
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
            QMessageBox::warning(this, tr("Please Wait"), tr("Currently fetching info for another download."));
        }
        return;
    }

    const QString type = options.value(QStringLiteral("type")).toString();
    QStringList missingBinaries;

    if (type == QStringLiteral("gallery")) {
        const QStringList required = {QStringLiteral("gallery-dl"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe")};
        for (const QString &bin : required) {
            const QString source = ProcessUtils::findBinary(bin, m_configManager).source;
            if (source == QStringLiteral("Not Found") || source == QStringLiteral("Invalid Custom")) {
                missingBinaries << bin;
            }
        }
    } else if (type == QStringLiteral("view_formats")) {
        const QString source = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager).source;
        if (source == QStringLiteral("Not Found") || source == QStringLiteral("Invalid Custom")) {
            missingBinaries << QStringLiteral("yt-dlp");
        }
    } else {
        const QStringList required = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("deno")};
        for (const QString &bin : required) {
            const QString source = ProcessUtils::findBinary(bin, m_configManager).source;
            if (source == QStringLiteral("Not Found") || source == QStringLiteral("Invalid Custom")) {
                missingBinaries << bin;
            }
        }

        const bool useAria2c = m_configManager->get(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false).toBool();
        if (useAria2c) {
            const QString ariaSource = ProcessUtils::findBinary(QStringLiteral("aria2c"), m_configManager).source;
            if (ariaSource == QStringLiteral("Not Found") || ariaSource == QStringLiteral("Invalid Custom")) {
                qWarning() << "aria2c is enabled but missing. Auto-reverting to yt-dlp native downloader.";
                m_configManager->set(QStringLiteral("Metadata"), QStringLiteral("use_aria2c"), false);
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
        const QStringList requiredAfterSetup = type == QStringLiteral("gallery")
            ? QStringList{QStringLiteral("gallery-dl"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe")}
            : type == QStringLiteral("view_formats")
                ? QStringList{QStringLiteral("yt-dlp")}
                : QStringList{QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("deno")};
        for (const QString &bin : requiredAfterSetup) {
            const QString source = ProcessUtils::resolveBinary(bin, m_configManager).source;
            if (source == QStringLiteral("Not Found") || source == QStringLiteral("Invalid Custom")) {
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

    const bool overrideArchive = mutableOptions.value(QStringLiteral("override_archive"), m_configManager->get(QStringLiteral("General"), QStringLiteral("override_archive"), false)).toBool();

    if (!overrideArchive && m_archiveManager && m_archiveManager->isInArchive(url)) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, tr("Duplicate Download"),
                                      tr("The following URL is already in your download history:\n%1\n\nDo you want to download it again?").arg(url),
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            return;
        }
        mutableOptions.insert(QStringLiteral("override_archive"), true);
    }

    const bool runtimeSubs = m_configManager->get(QStringLiteral("Subtitles"), QStringLiteral("languages"), QStringLiteral("en")).toString().split(QLatin1Char(',')).contains(QStringLiteral("runtime"));

    if (runtimeSubs && !nonInteractive) {
        m_pendingUrl = url;
        m_pendingOptions = mutableOptions;
        statusBar()->showMessage(tr("Fetching media info for runtime selection..."));
        const QString ytDlpPath = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager).path;
        QStringList args;
        args << QStringLiteral("--dump-json") << QStringLiteral("--no-playlist") << url;
        m_runtimeExtractor->extract(ytDlpPath, args);
        return;
    }

    static const QRegularExpression fastTrackRe(QStringLiteral(R"(^(https?://)?(www\.)?(youtube\.com|youtu\.be|music\.youtube\.com|tiktok\.com|instagram\.com|twitter\.com|x\.com)/)"));
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
    const bool runtimeSubs = m_configManager->get(QStringLiteral("Subtitles"), QStringLiteral("languages"), QStringLiteral("en")).toString().split(QLatin1Char(',')).contains(QStringLiteral("runtime"));

    RuntimeSelectionDialog dialog(info, false, false, runtimeSubs, this);
    if (dialog.exec() == QDialog::Accepted) {
        QVariantMap opts = m_pendingOptions;
        if (runtimeSubs) {
            const QStringList subs = dialog.getSelectedSubtitles();
            if (!subs.isEmpty()) opts.insert(QStringLiteral("runtime_subtitles"), subs.join(QLatin1Char(',')));
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
        QMessageBox::warning(this, tr("Extraction Error"), tr("Failed to fetch media info for runtime selection:\n%1").arg(error));
    }
    m_pendingUrl.clear();
    m_pendingOptions.clear();
}

void MainWindow::onDownloadSectionsRequested(const QString &url, const QVariantMap &options, const QVariantMap &infoJson)
{
    if (MainWindowHelpers::isNonInteractiveRequest(options)) {
        QVariantMap newOptions = options;
        newOptions.insert(QStringLiteral("download_sections_set"), true);
        qInfo() << "Skipping download sections dialog for non-interactive request:" << url;
        m_downloadManager->enqueueDownload(url, newOptions);
        m_uiBuilder->tabWidget()->setCurrentWidget(m_activeDownloadsTab);
        return;
    }

    DownloadSectionsDialog dialog(infoJson, this);
    if (dialog.exec() == QDialog::Accepted) {
        const QString sections = dialog.getSectionsString();
        const QString sectionLabel = dialog.getFilenameLabel();
        QVariantMap newOptions = options;
        newOptions.insert(QStringLiteral("download_sections_set"), true);
        if (!sections.isEmpty()) {
            newOptions.insert(QStringLiteral("download_sections"), sections);
        }
        if (!sectionLabel.isEmpty()) {
            newOptions.insert(QStringLiteral("download_sections_label"), sectionLabel);
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
            QMessageBox::warning(this, tr("Invalid URL"), tr("The URL could not be validated:\n%1").arg(error));
        }
    }
    m_pendingUrl.clear();
    m_pendingOptions.clear();
}

void MainWindow::onYtDlpErrorPopup(const QString &id, const QString &errorType, const QString &userMessage, const QString &rawError, const QVariantMap &itemData)
{
    Q_UNUSED(id);

    const QString url = itemData.value(QStringLiteral("url")).toString();
    const QVariantMap requestOptions = itemData.value(QStringLiteral("options")).toMap();
    const bool nonInteractive = MainWindowHelpers::isNonInteractiveRequest(requestOptions);
    const QString urlHtml = url.isEmpty() ? QString() : QStringLiteral("<br><br><a href=\"%1\">%1</a>").arg(url.toHtmlEscaped());
    const QString richUserMessage = userMessage.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>"));

    QString cleanError = rawError;
    if (cleanError.startsWith(QStringLiteral("ERROR: "))) {
        cleanError = cleanError.mid(7);
    }
    cleanError.remove(QRegularExpression(QStringLiteral("^\\[[^\\]]+\\]\\s*")));

    if (errorType == QStringLiteral("scheduled_livestream")) {
        if (nonInteractive) {
            QVariantMap newItemData = itemData;
            QVariantMap options = requestOptions;
            options.insert(QStringLiteral("wait_for_video"), true);
            options.insert(QStringLiteral("livestream_wait_min"), m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_min"), 60).toInt());
            options.insert(QStringLiteral("livestream_wait_max"), m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_max"), 300).toInt());
            MainWindowHelpers::applyNonInteractiveDownloadDefaults(options);
            newItemData.insert(QStringLiteral("options"), options);
            qInfo() << "Automatically waiting for scheduled livestream in non-interactive request:" << url;
            m_downloadManager->restartDownloadWithOptions(newItemData);
            return;
        }

        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Scheduled Livestream"));
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        msgBox.setText(QStringLiteral("%1%2").arg(richUserMessage, urlHtml));
        if (!cleanError.isEmpty()) {
            msgBox.setInformativeText(cleanError.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
        }
        msgBox.setIcon(QMessageBox::Information);

        QPushButton *waitButton = msgBox.addButton(tr("Wait and Download When Available"), QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Cancel);

        msgBox.exec();

        if (msgBox.clickedButton() == waitButton) {
            QVariantMap newItemData = itemData;
            QVariantMap options = newItemData[QStringLiteral("options")].toMap();
            options.insert(QStringLiteral("wait_for_video"), true);

            int minWait;
            int maxWait;
            if (cleanError.contains(QStringLiteral("days"), Qt::CaseInsensitive) || cleanError.contains(QStringLiteral("hours"), Qt::CaseInsensitive)) {
                minWait = 1800;
                maxWait = 3600;
            } else if (cleanError.contains(QStringLiteral("minutes"), Qt::CaseInsensitive)) {
                minWait = 60;
                maxWait = 300;
            } else {
                minWait = m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_min"), 60).toInt();
                maxWait = m_configManager->get(QStringLiteral("Livestream"), QStringLiteral("wait_for_video_max"), 300).toInt();
            }

            options.insert(QStringLiteral("livestream_wait_min"), minWait);
            options.insert(QStringLiteral("livestream_wait_max"), maxWait);
            newItemData.insert(QStringLiteral("options"), options);
            m_downloadManager->restartDownloadWithOptions(newItemData);
        }
    } else {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Download Error"));
        msgBox.setTextFormat(Qt::RichText);
        msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        msgBox.setText(QStringLiteral("%1%2").arg(richUserMessage, urlHtml));
        if (!cleanError.isEmpty()) {
            msgBox.setInformativeText(cleanError.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
        }
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.exec();
    }
}
