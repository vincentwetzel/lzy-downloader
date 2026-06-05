#include "StartTabUrlHandler.h"
#include <QGuiApplication>
#include <QUrl>
#include <QJsonArray>
#include <QJsonObject>
#include <QFocusEvent>
#include <QDebug>
#include <QMessageBox>
#include <QClipboard>
#include <QPushButton>
#include "core/ProcessUtils.h"

StartTabUrlHandler::StartTabUrlHandler(ConfigManager *configManager, ExtractorJsonParser *extractorJsonParser, StartTabUiBuilder *uiBuilder, QObject *parent)
    : QObject(parent),
      m_configManager(configManager),
      m_extractorJsonParser(extractorJsonParser),
      m_uiBuilder(uiBuilder),
      m_typeSelectionDialog(nullptr)
{
    if (!m_uiBuilder) {
        qCritical() << "CRITICAL ERROR: m_uiBuilder is null in StartTabUrlHandler constructor!";
        return;
    }
    // Connect URL input text changes to auto-switch download type
    if (m_uiBuilder->urlInput()) {
        connect(m_uiBuilder->urlInput(), &QTextEdit::textChanged, this, &StartTabUrlHandler::onUrlInputTextChanged);
    }
}

StartTabUrlHandler::~StartTabUrlHandler()
{
    if (m_typeSelectionDialog) {
        m_typeSelectionDialog->deleteLater();
    }
}

void StartTabUrlHandler::onExtractorsReady()
{
    if (!m_uiBuilder) return;
    if (m_uiBuilder->urlInput()) {
        m_uiBuilder->urlInput()->setEnabled(true);
        m_uiBuilder->urlInput()->setPlaceholderText(tr("Paste one or more media URLs (one per line)..."));
    }
}

bool StartTabUrlHandler::tryAutoPasteFromClipboard()
{
    return checkClipboardForUrl();
}

void StartTabUrlHandler::focusUrlInput()
{
    if (!m_uiBuilder) return;
    if (m_uiBuilder->urlInput()) {
        m_uiBuilder->urlInput()->setFocus(Qt::OtherFocusReason);
    }
}

void StartTabUrlHandler::handleFocusInEvent(QFocusEvent *event)
{
    if (event->reason() == Qt::MouseFocusReason)
    {
        checkClipboardForUrl();
    }
}

bool StartTabUrlHandler::handleEventFilter(QObject *obj, QEvent *event)
{
    if (!m_uiBuilder) return false;
    if (obj == m_uiBuilder->urlInput()) {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn) {
            checkClipboardForUrl();
        }
    }
    return false; // Let StartTab continue processing the event
}

void StartTabUrlHandler::onClipboardChangedWhileDialogIsOpen()
{
    if (!m_typeSelectionDialog) return;

    const QClipboard *clipboard = QGuiApplication::clipboard();
    QString text = clipboard->text().trimmed();
    if (text.isEmpty()) return;

    QString firstUrlStr = text.split('\n', Qt::SkipEmptyParts).first().trimmed();
    if (!firstUrlStr.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) && !firstUrlStr.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        firstUrlStr = QStringLiteral("https://%1").arg(firstUrlStr);
    }
    QUrl url(firstUrlStr);
    QString host = url.host().toLower();

    if (host.isEmpty()) return;

    QJsonObject ytDlpExtractors = m_extractorJsonParser->getYtDlpExtractors();
    bool inYtDlp = false;
    for (auto it = ytDlpExtractors.begin(); it != ytDlpExtractors.end(); ++it) {
        QJsonArray domains = it.value().toObject()["domains"].toArray();
        for (const QJsonValue &domainValue : domains) {
            QString domainStr = domainValue.toString().toLower();
            if (host == domainStr || host.endsWith(QStringLiteral(".") + domainStr)) {
                inYtDlp = true;
                break;
            }
        }
        if (inYtDlp) break;
    }

    if (!inYtDlp) {
        m_typeSelectionDialog->reject();
    }
}

void StartTabUrlHandler::onTypeSelectionDialogFinished(int /*result*/)
{
    if (!m_typeSelectionDialog) return;

    QObject::disconnect(QGuiApplication::clipboard(), &QClipboard::changed, this, &StartTabUrlHandler::onClipboardChangedWhileDialogIsOpen);

    QAbstractButton *clickedButton = m_typeSelectionDialog->clickedButton();
    if (clickedButton) {
        QString btnText = clickedButton->text();
        QString dataValue;
        if (btnText.startsWith(tr("Video"))) dataValue = QStringLiteral("video");
        else if (btnText.startsWith(tr("Audio Only"))) dataValue = QStringLiteral("audio");
        else if (btnText.startsWith(tr("View Video/Audio Formats"))) dataValue = QStringLiteral("formats");

        if (!dataValue.isEmpty() && m_uiBuilder && m_uiBuilder->downloadTypeCombo()) {
            int index = m_uiBuilder->downloadTypeCombo()->findData(dataValue);
            if (index != -1) {
                m_uiBuilder->downloadTypeCombo()->setCurrentIndex(index);
            }
        }
    }

    m_typeSelectionDialog->deleteLater();
    m_typeSelectionDialog = nullptr;
}

bool StartTabUrlHandler::checkClipboardForUrl()
{
    if (!m_uiBuilder) return false;
    if (!m_uiBuilder->urlInput() || !m_uiBuilder->urlInput()->isEnabled()) {
        return false;
    }

    const QClipboard *clipboard = QGuiApplication::clipboard();
    const QString text = clipboard->text().trimmed();
    if (text.isEmpty() || m_uiBuilder->urlInput()->toPlainText().trimmed() == text) {
        return false;
    }

    QJsonObject ytDlpExtractors = m_extractorJsonParser->getYtDlpExtractors();
    QJsonObject galleryDlExtractors = m_extractorJsonParser->getGalleryDlExtractors();

    if (ytDlpExtractors.isEmpty() && galleryDlExtractors.isEmpty()) {
        return false;
    }

    QString firstUrlStr = text.split('\n', Qt::SkipEmptyParts).first().trimmed();
    if (!firstUrlStr.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) && !firstUrlStr.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        firstUrlStr = QStringLiteral("https://%1").arg(firstUrlStr);
    }
    QUrl url(firstUrlStr);
    QString host = url.host().toLower();

    if (host.isEmpty()) {
        return false;
    }

    ExtractorSupport support = checkUrlExtractorSupport(text);

    if (support != ExtractorSupport::None) {
        // Clipboard-driven auto-paste should mirror the clipboard contents exactly.
        // Replacing the field here prevents stale URLs from being concatenated when
        // the user copies a second link while auto-paste is enabled.
        if (m_uiBuilder->urlInput()->toPlainText().trimmed() != text) {
            m_uiBuilder->urlInput()->setPlainText(text);
        }

        if (support == ExtractorSupport::GalleryDlOnly) {
            if (m_uiBuilder->downloadTypeCombo()) {
                int galleryIndex = m_uiBuilder->downloadTypeCombo()->findData(QStringLiteral("gallery"));
                m_uiBuilder->downloadTypeCombo()->setCurrentIndex(galleryIndex);
                m_lastAutoSwitchedUrl = text;
            }
        } else if (support == ExtractorSupport::YtDlpOnly) {
            if (m_uiBuilder->downloadTypeCombo() && m_uiBuilder->downloadTypeCombo()->currentData().toString() == QStringLiteral("gallery")) {
                if (m_typeSelectionDialog) {
                    return true;
                }

                m_typeSelectionDialog = new QMessageBox(qobject_cast<QWidget*>(parent())); // Parent is StartTab
                m_typeSelectionDialog->setWindowTitle(tr("Download Type Selection"));
                m_typeSelectionDialog->setText(tr("The pasted URL is likely for video/audio.\nPlease select a download type:"));
                m_typeSelectionDialog->setIcon(QMessageBox::Question);

                QStringList requiredYt = {QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"), QStringLiteral("deno")};
                bool hasMissingYt = false;
                for (const QString &bin : requiredYt) {
                    QString source = ProcessUtils::findBinary(bin, m_configManager).source;
                    if (source == QStringLiteral("Not Found") || source == QStringLiteral("Invalid Custom")) {
                        hasMissingYt = true;
                        break;
                    }
                }
                
                QString ytSource = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager).source;
                bool ytDlpOnlyMissing = (ytSource == QStringLiteral("Not Found") || ytSource == QStringLiteral("Invalid Custom"));

                m_typeSelectionDialog->addButton(hasMissingYt ? tr("Video (missing binaries)") : tr("Video"), QMessageBox::ActionRole);
                m_typeSelectionDialog->addButton(hasMissingYt ? tr("Audio Only (missing binaries)") : tr("Audio Only"), QMessageBox::ActionRole);
                m_typeSelectionDialog->addButton(ytDlpOnlyMissing ? tr("View Video/Audio Formats (missing binaries)") : tr("View Video/Audio Formats"), QMessageBox::ActionRole);
                m_typeSelectionDialog->addButton(QMessageBox::Cancel);

                connect(m_typeSelectionDialog, &QDialog::finished, this, &StartTabUrlHandler::onTypeSelectionDialogFinished);
                connect(QGuiApplication::clipboard(), &QClipboard::changed, this, &StartTabUrlHandler::onClipboardChangedWhileDialogIsOpen);

                m_typeSelectionDialog->open();
            }
        }
        return true;
    }

    return false;
}

StartTabUrlHandler::ExtractorSupport StartTabUrlHandler::checkUrlExtractorSupport(const QString &url) const
{
    QJsonObject ytDlpExtractors = m_extractorJsonParser->getYtDlpExtractors();
    QJsonObject galleryDlExtractors = m_extractorJsonParser->getGalleryDlExtractors();

    QString firstUrlStr = url.split('\n', Qt::SkipEmptyParts).first().trimmed();
    if (!firstUrlStr.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) && !firstUrlStr.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        firstUrlStr = QStringLiteral("https://%1").arg(firstUrlStr);
    }
    QUrl parsedUrl(firstUrlStr);
    QString host = parsedUrl.host().toLower();

    if (host.isEmpty()) {
        return ExtractorSupport::None;
    }

    bool inYtDlp = false;
    for (auto it = ytDlpExtractors.begin(); it != ytDlpExtractors.end(); ++it) {
        QJsonArray domains = it.value().toObject()["domains"].toArray();
        for (const QJsonValue &domainValue : domains) {
            QString domainStr = domainValue.toString().toLower();
            if (host == domainStr || host.endsWith(QStringLiteral(".") + domainStr)) {
                inYtDlp = true;
                break;
            }
        }
        if (inYtDlp) break;
    }

    bool inGalleryDl = false;
    for (auto it = galleryDlExtractors.begin(); it != galleryDlExtractors.end(); ++it) {
        QJsonArray domains = it.value().toObject()["domains"].toArray();
        for (const QJsonValue &domainValue : domains) {
            QString domainStr = domainValue.toString().toLower();
            if (host == domainStr || host.endsWith(QStringLiteral(".") + domainStr)) {
                inGalleryDl = true;
                break;
            }
        }
        if (inGalleryDl) break;
    }

    if (inYtDlp && inGalleryDl) {
        return ExtractorSupport::Both;
    } else if (inYtDlp) {
        return ExtractorSupport::YtDlpOnly;
    } else if (inGalleryDl) {
        return ExtractorSupport::GalleryDlOnly;
    } else {
        return ExtractorSupport::None;
    }
}

void StartTabUrlHandler::autoSwitchDownloadType(const QString &url)
{
    if (!m_uiBuilder) return;
    if (url.isEmpty() || m_lastAutoSwitchedUrl == url) {
        return;
    }

    ExtractorSupport support = checkUrlExtractorSupport(url);

    switch (support) {
        case ExtractorSupport::YtDlpOnly:
            if (m_uiBuilder->downloadTypeCombo() && m_uiBuilder->downloadTypeCombo()->currentData().toString() == QStringLiteral("gallery")) {
                int videoIndex = m_uiBuilder->downloadTypeCombo()->findData(QStringLiteral("video"));
                if (videoIndex != -1) {
                    m_uiBuilder->downloadTypeCombo()->setCurrentIndex(videoIndex);
                    m_lastAutoSwitchedUrl = url;
                }
            }
            break;

        case ExtractorSupport::GalleryDlOnly:
            if (m_uiBuilder->downloadTypeCombo()) {
                int galleryIndex = m_uiBuilder->downloadTypeCombo()->findData(QStringLiteral("gallery"));
                if (galleryIndex != -1) {
                    m_uiBuilder->downloadTypeCombo()->setCurrentIndex(galleryIndex);
                    m_lastAutoSwitchedUrl = url;
                }
            }
            break;

        case ExtractorSupport::Both:
        case ExtractorSupport::None:
            // Do nothing, let user decide
            break;
    }
}

void StartTabUrlHandler::onUrlInputTextChanged()
{
    if (!m_uiBuilder || !m_uiBuilder->urlInput()) {
        return;
    }
    const QString text = m_uiBuilder->urlInput()->toPlainText();
    QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) || trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        autoSwitchDownloadType(trimmed);
    }
    emit urlInputTextChanged(text); // Re-emit for StartTab's internal connections
}
