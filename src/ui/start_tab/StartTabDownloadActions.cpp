#include "StartTabDownloadActions.h"
#include <QDebug>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>
#include <QDesktopServices>
#include <QDir>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QDialog>
#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include "ui/ToggleSwitch.h"
#include "core/ProcessUtils.h"
#include <QTimer>

StartTabDownloadActions::StartTabDownloadActions(ConfigManager *configManager, StartTabUiBuilder *uiBuilder,
                                                 YtDlpArgsBuilder *ytDlpArgsBuilder, GalleryDlArgsBuilder *galleryDlArgsBuilder,
                                                 QWidget *parent)
    : QObject(parent),
      m_configManager(configManager),
      m_uiBuilder(uiBuilder),
      m_ytDlpArgsBuilder(ytDlpArgsBuilder),
      m_galleryDlArgsBuilder(galleryDlArgsBuilder), // Keep for future use if needed, or remove if truly unused
      m_parentWidget(parent)
{
    if (!m_uiBuilder) {
        qCritical() << "CRITICAL ERROR: m_uiBuilder is null in StartTabDownloadActions constructor!";
        return;
    }
    if (!m_configManager) {
        qCritical() << "CRITICAL ERROR: m_configManager is null in StartTabDownloadActions constructor!";
        return;
    }

    // Connect UI elements to slots
    if (m_uiBuilder->downloadButton()) {
        connect(m_uiBuilder->downloadButton(), &QPushButton::clicked, this, &StartTabDownloadActions::onDownloadButtonClicked);
    }
    if (m_uiBuilder->downloadTypeCombo()) {
        connect(m_uiBuilder->downloadTypeCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StartTabDownloadActions::onDownloadTypeChanged);
    }
    if (m_uiBuilder->openDownloadsFolderButton()) {
        connect(m_uiBuilder->openDownloadsFolderButton(), &QPushButton::clicked, this, &StartTabDownloadActions::openDownloadsFolder);
    }

    connect(m_configManager, &ConfigManager::settingChanged, this, [this](const QString &group, const QString &/*key*/, const QVariant &/*value*/) {
        if (group == QStringLiteral("Binaries") || group == QStringLiteral("General")) {
            QTimer::singleShot(0, this, &StartTabDownloadActions::updateDynamicUI);
        }
    });
}

StartTabDownloadActions::~StartTabDownloadActions()
{
    // No owned pointers to delete
}

void StartTabDownloadActions::onDownloadButtonClicked() {
    qDebug() << "StartTabDownloadActions::onDownloadButtonClicked called.";

    if (!m_uiBuilder->urlInput()) {
        qCritical() << "CRITICAL ERROR: m_urlInput is null in onDownloadButtonClicked!";
        return;
    }
    QString urlStr = m_uiBuilder->urlInput()->toPlainText().trimmed();
    qDebug() << "m_urlInput accessed. urlStr:" << urlStr;

    if (urlStr.isEmpty()) {
        QMessageBox::warning(m_parentWidget, "Input Error", "Please enter a valid URL(s).");
        return;
    }

    QStringList urls = urlStr.split('\n', Qt::SkipEmptyParts);
    if (urls.isEmpty()) {
        QMessageBox::warning(m_parentWidget, "Input Error", "Please enter a valid URL(s).");
        return;
    }

    if (!m_uiBuilder->playlistLogicCombo() || !m_uiBuilder->maxConcurrentCombo() || !m_uiBuilder->rateLimitCombo() || !m_uiBuilder->overrideDuplicateCheck()) {
        qCritical() << "CRITICAL ERROR: One or more UI elements are null in onDownloadButtonClicked!";
        return;
    }

    m_configManager->set("General", "playlist_logic", m_uiBuilder->playlistLogicCombo()->currentText());
    m_configManager->set("General", "max_threads", m_uiBuilder->maxConcurrentCombo()->currentText());
    m_configManager->set("General", "rate_limit", m_uiBuilder->rateLimitCombo()->currentText());
    m_configManager->set("General", "override_archive", m_uiBuilder->overrideDuplicateCheck()->isChecked());
    m_configManager->save();

    if (!m_uiBuilder->downloadTypeCombo()) {
        qCritical() << "CRITICAL ERROR: m_downloadTypeCombo is null in onDownloadButtonClicked!";
        return;
    }
    QString type = m_uiBuilder->downloadTypeCombo()->currentData().toString();
    qDebug() << "m_downloadTypeCombo accessed. type:" << type;

    if (type == "formats") {
        checkFormats(urls.first());
        return;
    }

    QVariantMap options;
    options["type"] = type;

    for (const QString &singleUrl : urls) {
        QUrl url(singleUrl);
        if (!url.isValid()) {
            QMessageBox::warning(m_parentWidget, "Input Error", "The URL entered is invalid: " + singleUrl);
            continue;
        }
        emit downloadRequested(singleUrl, options);
    }

    m_uiBuilder->urlInput()->clear();
    qDebug() << "StartTabDownloadActions::onDownloadButtonClicked finished.";
}

void StartTabDownloadActions::onDownloadTypeChanged(int index)
{
    if (!m_uiBuilder->downloadTypeCombo() || !m_uiBuilder->downloadButton()) {
        qCritical() << "CRITICAL ERROR: m_downloadTypeCombo or m_downloadButton is null in onDownloadTypeChanged!";
        return;
    }
    QString type = m_uiBuilder->downloadTypeCombo()->itemData(index).toString();
    
    if (type == "video") {
        m_uiBuilder->downloadButton()->setText("Download Video");
        m_uiBuilder->downloadButton()->setToolTip("Click to add the URL(s) to the download queue.");
    } else if (type == "audio") {
        m_uiBuilder->downloadButton()->setText("Download Audio");
        m_uiBuilder->downloadButton()->setToolTip("Click to add the URL(s) to the download queue.");
    } else if (type == "gallery") {
        m_uiBuilder->downloadButton()->setText("Download Gallery");
        m_uiBuilder->downloadButton()->setToolTip("Click to add the URL(s) to the download queue.");
    } else if (type == "formats") {
        m_uiBuilder->downloadButton()->setText("View Video/Audio Formats");
        m_uiBuilder->downloadButton()->setToolTip("Query yt-dlp to list available video and audio formats without downloading. Not supported for galleries.");
    }
    
    emit updateCommandPreview();
}

void StartTabDownloadActions::checkFormats(const QString &url) {
    if (!m_uiBuilder->downloadButton()) {
        qCritical() << "CRITICAL ERROR: m_downloadButton is null in checkFormats!";
        return;
    }
    m_uiBuilder->downloadButton()->setEnabled(false);
    m_uiBuilder->downloadButton()->setText("Checking...");

    QProcess *process = new QProcess(this);
    connect(process, &QProcess::finished, this, &StartTabDownloadActions::onViewFormatsFinished);

    QStringList args;
    args << url << "-F";

    QString cookiesBrowser = m_configManager->get("General", "cookies_from_browser", "None").toString();
    if (cookiesBrowser != "None") {
        args << "--cookies-from-browser" << cookiesBrowser;
    }

    const QString ytDlpPath = resolveExecutablePath("yt-dlp.exe");
    if (ytDlpPath.isEmpty()) {
        if (m_uiBuilder->downloadButton()) {
            m_uiBuilder->downloadButton()->setEnabled(true);
        }
        if (m_uiBuilder->downloadTypeCombo()) {
            onDownloadTypeChanged(m_uiBuilder->downloadTypeCombo()->currentIndex());
        }

        emit missingBinariesDetected({"yt-dlp"});
        process->deleteLater();
        return;
    }

    process->start(ytDlpPath, args);
}

void StartTabDownloadActions::onViewFormatsFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    QProcess *process = qobject_cast<QProcess*>(sender());
    if (m_uiBuilder->downloadButton()) {
        m_uiBuilder->downloadButton()->setEnabled(true);
    }
    if (m_uiBuilder->downloadTypeCombo()) {
        onDownloadTypeChanged(m_uiBuilder->downloadTypeCombo()->currentIndex());
    }

    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        QString output = process->readAllStandardOutput();

        QDialog dialog(m_parentWidget);
        dialog.setWindowTitle("Available Formats");
        dialog.resize(600, 400);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        QTextEdit *textEdit = new QTextEdit(&dialog);
        textEdit->setReadOnly(true);
        textEdit->setFontFamily("Courier New");
        textEdit->setText(output);
        layout->addWidget(textEdit);

        QPushButton *closeButton = new QPushButton("Close", &dialog);
        connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        layout->addWidget(closeButton);

        dialog.exec();
    } else {
        QString error = process->readAllStandardError();
        QMessageBox::critical(m_parentWidget, "Error", "Failed to retrieve formats:\n" + error);
    }

    process->deleteLater();
}

void StartTabDownloadActions::openDownloadsFolder() {
    QString completedDir = m_configManager->get("Paths", "completed_downloads_directory").toString();
    if (completedDir.isEmpty() || !QDir(completedDir).exists()) {
        QMessageBox::warning(m_parentWidget, "Folder Not Found",
                             "The completed downloads directory is not set or does not exist.\n"
                             "Please configure it in the Advanced Settings tab.");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(completedDir));
}

void StartTabDownloadActions::updateDynamicUI() {
    if (!m_uiBuilder || !m_uiBuilder->downloadTypeCombo() || !m_uiBuilder->downloadButton()) {
        qWarning() << "StartTabDownloadActions::updateDynamicUI - UI elements not fully initialized yet.";
        return;
    }

    QStringList requiredYt = {"yt-dlp", "ffmpeg", "ffprobe", "deno"};
    bool hasMissingYt = false;
    for (const QString &bin : requiredYt) {
        QString source = ProcessUtils::findBinary(bin, m_configManager).source;
        if (source == "Not Found" || source == "Invalid Custom") {
            hasMissingYt = true;
            break;
        }
    }

    QStringList requiredGallery = {"gallery-dl", "ffmpeg", "ffprobe"};
    bool hasMissingGallery = false;
    for (const QString &bin : requiredGallery) {
        QString source = ProcessUtils::findBinary(bin, m_configManager).source;
        if (source == "Not Found" || source == "Invalid Custom") {
            hasMissingGallery = true;
            break;
        }
    }

    QString ytSource = ProcessUtils::findBinary("yt-dlp", m_configManager).source;
    bool ytDlpOnlyMissing = (ytSource == "Not Found" || ytSource == "Invalid Custom");

    auto updateItemText = [this](const QString &dataValue, const QString &baseText, bool isMissing) {
        int index = m_uiBuilder->downloadTypeCombo()->findData(dataValue);
        if (index != -1) {
            QString newText = baseText + (isMissing ? " (missing binaries)" : "");
            m_uiBuilder->downloadTypeCombo()->setItemText(index, newText);
        }
    };

    updateItemText("video", "Video", hasMissingYt);
    updateItemText("audio", "Audio Only", hasMissingYt);
    updateItemText("gallery", "Gallery", hasMissingGallery);
    updateItemText("formats", "View Video/Audio Formats", ytDlpOnlyMissing);

    // Update cookie warning label visibility if it exists in the UI
    QLabel *cookieWarning = m_parentWidget->findChild<QLabel*>("cookieWarningLabel");
    if (cookieWarning) {
        QString cookies = m_configManager->get("General", "cookies_from_browser", "None").toString();
        cookieWarning->setVisible(cookies.compare("None", Qt::CaseInsensitive) == 0 || cookies.trimmed().isEmpty());
    }

    m_uiBuilder->downloadButton()->setStyleSheet("QPushButton { font-size: 16px; font-weight: bold; background-color: #0078d7; color: white; border-radius: 5px; padding: 10px; } QPushButton:hover { background-color: #005a9e; } QPushButton:pressed { background-color: #004578; }");
    m_uiBuilder->downloadButton()->setToolTip("Click to add the URL(s) to the download queue.");

    int index = m_uiBuilder->downloadTypeCombo()->currentIndex();
    onDownloadTypeChanged(index);
}

QString StartTabDownloadActions::resolveExecutablePath(const QString &name) const {
    QString baseName = name;
    if (baseName.endsWith(".exe", Qt::CaseInsensitive)) {
        baseName.chop(4);
    }

    const ProcessUtils::FoundBinary binary = ProcessUtils::findBinary(baseName, m_configManager);
    if (binary.source != "Not Found" && !binary.path.isEmpty()) {
        return binary.path;
    }

    return QString();
}
