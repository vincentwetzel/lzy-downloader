#include "OutputTemplatesPage.h"
#include "core/ConfigManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QProcess>
#include <QSignalBlocker>
#include "core/ProcessUtils.h"

namespace {
    bool validateYtDlpTemplate(QWidget* parent, ConfigManager* configManager, const QString& templateStr) {
        QString ytDlpPath = ProcessUtils::findBinary("yt-dlp", configManager).path;
        if (ytDlpPath.isEmpty()) {
            QMessageBox::warning(parent, "Missing Binary", "yt-dlp executable not found. Cannot validate template.");
            return false;
        }

        QProcess process;
        ProcessUtils::setProcessEnvironment(process);
        process.start(ytDlpPath, QStringList() << "-o" << templateStr << "dummy:");
        if (!process.waitForStarted(2000)) {
            QMessageBox::warning(parent, "Process Error", "Failed to start yt-dlp to validate the template. Please check your yt-dlp installation.");
            return false;
        }
        if (!process.waitForFinished(5000)) {
            ProcessUtils::terminateProcessTree(&process);
            QMessageBox::warning(parent, "Validation Timeout", "yt-dlp took too long to validate the template (exceeded 5 seconds).");
            return false;
        }
        QString err = process.readAllStandardError();
        if (err.contains("error:", Qt::CaseInsensitive) && (err.contains("template", Qt::CaseInsensitive) || err.contains("missing", Qt::CaseInsensitive))) {
            QMessageBox::warning(parent, "Invalid Template", "yt-dlp rejected the template:\n" + err.trimmed());
            return false;
        }
        return true;
    }
}

OutputTemplatesPage::OutputTemplatesPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QStringList ytDlpTokens = {
        "%(title)s", "%(uploader)s", "%(upload_date>%Y-%m-%d)s", "%(id)s", "%(ext)s",
        "%(playlist_index)s", "%(playlist_title)s", "%(channel)s", "%(channel_id)s",
        "%(duration)s", "%(view_count)s", "%(like_count)s", "%(comment_count)s",
        "%(age_limit)s", "%(genre)s", "%(format_id)s", "%(format)s", "%(format_note)s",
        "%(resolution)s", "%(width)s", "%(height)s", "%(fps)s", "%(vcodec)s", "%(acodec)s",
        "%(abr)s", "%(vbr)s", "%(tbr)s", "%(filesize)s", "%(epoch)s", "%(autonumber)s"
    };

    QGroupBox *ytDlpGroup = new QGroupBox("yt-dlp Filename Templates", this);
    ytDlpGroup->setToolTip("Define custom filename patterns for video and audio downloads using yt-dlp templates.");
    QGridLayout *ytDlpLayout = new QGridLayout(ytDlpGroup);

    QLabel* videoPatternLabel = new QLabel("Video Pattern:", this);
    videoPatternLabel->setToolTip("The filename pattern for video downloads.");
    ytDlpLayout->addWidget(videoPatternLabel, 0, 0);
    m_videoOutputTemplateInput = new QLineEdit(this);
    m_videoOutputTemplateInput->setMinimumHeight(30);
    m_videoOutputTemplateInput->setToolTip("The filename pattern for video downloads.");
    ytDlpLayout->addWidget(m_videoOutputTemplateInput, 0, 1);
    m_videoTemplateTokensCombo = new QComboBox(this);
    m_videoTemplateTokensCombo->addItem("Insert token...", "");
    m_videoTemplateTokensCombo->setMaximumWidth(180);
    m_videoTemplateTokensCombo->setToolTip("Insert a yt-dlp metadata token into the video template.");
    m_videoTemplateTokensCombo->addItems(ytDlpTokens);
    ytDlpLayout->addWidget(m_videoTemplateTokensCombo, 0, 2);
    m_saveVideoTemplateButton = new QPushButton("Save", this);
    m_saveVideoTemplateButton->setToolTip("Save and validate the video template.");
    ytDlpLayout->addWidget(m_saveVideoTemplateButton, 0, 3);
    QPushButton *resetVideoButton = new QPushButton("Reset", this);
    resetVideoButton->setToolTip("Reset the video template to its default value.");
    ytDlpLayout->addWidget(resetVideoButton, 0, 4);

    QLabel* audioPatternLabel = new QLabel("Audio Pattern:", this);
    audioPatternLabel->setToolTip("The filename pattern for audio downloads.");
    ytDlpLayout->addWidget(audioPatternLabel, 1, 0);
    m_audioOutputTemplateInput = new QLineEdit(this);
    m_audioOutputTemplateInput->setMinimumHeight(30);
    m_audioOutputTemplateInput->setToolTip("The filename pattern for audio downloads.");
    ytDlpLayout->addWidget(m_audioOutputTemplateInput, 1, 1);
    m_audioTemplateTokensCombo = new QComboBox(this);
    m_audioTemplateTokensCombo->addItem("Insert token...", "");
    m_audioTemplateTokensCombo->setMaximumWidth(180);
    m_audioTemplateTokensCombo->setToolTip("Insert a yt-dlp metadata token into the audio template.");
    m_audioTemplateTokensCombo->addItems(ytDlpTokens);
    ytDlpLayout->addWidget(m_audioTemplateTokensCombo, 1, 2);
    m_saveAudioTemplateButton = new QPushButton("Save", this);
    m_saveAudioTemplateButton->setToolTip("Save and validate the audio template.");
    ytDlpLayout->addWidget(m_saveAudioTemplateButton, 1, 3);
    QPushButton *resetAudioButton = new QPushButton("Reset", this);
    resetAudioButton->setToolTip("Reset the audio template to its default value.");
    ytDlpLayout->addWidget(resetAudioButton, 1, 4);
    layout->addWidget(ytDlpGroup);

    QGroupBox *galleryDlGroup = new QGroupBox("gallery-dl Filename Template", this);
    galleryDlGroup->setToolTip("Define custom filename patterns for gallery downloads using gallery-dl templates.");
    QHBoxLayout *galleryDlControlsLayout = new QHBoxLayout(galleryDlGroup);
    QLabel* galleryPatternLabel = new QLabel("Gallery Pattern:", this);
    galleryPatternLabel->setToolTip("The filename pattern for gallery downloads.");
    galleryDlControlsLayout->addWidget(galleryPatternLabel);
    m_galleryDlOutputTemplateInput = new QLineEdit(this);
    m_galleryDlOutputTemplateInput->setMinimumHeight(30);
    m_galleryDlOutputTemplateInput->setToolTip("The filename pattern for gallery downloads.");
    galleryDlControlsLayout->addWidget(m_galleryDlOutputTemplateInput, 1); // Stretch factor 1
    m_galleryDlTemplateTokensCombo = new QComboBox(this);
    m_galleryDlTemplateTokensCombo->addItem("Insert token...", "");
    m_galleryDlTemplateTokensCombo->setMaximumWidth(180);
    m_galleryDlTemplateTokensCombo->setToolTip("Insert a gallery-dl metadata token into the gallery template.");
    m_galleryDlTemplateTokensCombo->addItems({
        "{category}", "{subcategory}", "{id}", "{filename}", "{extension}",
        "{title}", "{description}", "{date}", "{date:%Y-%m-%d}",
        "{user[username]}", "{user[name]}", "{user[id]}",
        "{author[name]}", "{author[url]}", "{author[id]}",
        "{url}", "{shortcode}", "{num}", "{count}",
        "{width}", "{height}", "{size}", "{width}x{height}",
        "{post[title]}", "{post[id]}", "{post[num]}", "{post[count]}",
        "{media[num]}", "{media[count]}",
        "{category}/{id}_{filename}.{extension}"
    });
    galleryDlControlsLayout->addWidget(m_galleryDlTemplateTokensCombo, 0); // No stretch
    m_saveGalleryDlTemplateButton = new QPushButton("Save", this);
    m_saveGalleryDlTemplateButton->setToolTip("Save the gallery template.");
    galleryDlControlsLayout->addWidget(m_saveGalleryDlTemplateButton);
    QPushButton *resetGalleryDlButton = new QPushButton("Reset", this);
    resetGalleryDlButton->setToolTip("Reset the gallery template to its default value.");
    galleryDlControlsLayout->addWidget(resetGalleryDlButton);
    layout->addWidget(galleryDlGroup);

    layout->addStretch();

    connect(resetVideoButton, &QPushButton::clicked, this, [this]() {
        m_videoOutputTemplateInput->setText(m_configManager->get("General", "output_template").toString());
        QMessageBox::information(this, "Template Reset", "Video filename pattern has been reset to default.");
    });
    connect(resetAudioButton, &QPushButton::clicked, this, [this]() {
        m_audioOutputTemplateInput->setText(m_configManager->get("General", "output_template").toString());
        QMessageBox::information(this, "Template Reset", "Audio filename pattern has been reset to default.");
    });
    connect(resetGalleryDlButton, &QPushButton::clicked, this, [this]() {
        QString defaultTpl = m_configManager->getDefault("General", "gallery_output_template").toString();
        m_galleryDlOutputTemplateInput->setText(defaultTpl);
        QMessageBox::information(this, "Template Reset", "Gallery filename pattern has been reset to default.");
    });

    auto insertToken = [](QLineEdit* lineEdit, QComboBox* comboBox, int index) {
        if (index > 0) {
            lineEdit->insert(comboBox->itemText(index));
        }
        comboBox->setCurrentIndex(0);
    };

    connect(m_saveVideoTemplateButton, &QPushButton::clicked, this, &OutputTemplatesPage::validateAndSaveVideoTemplate);
    connect(m_videoTemplateTokensCombo, QOverload<int>::of(&QComboBox::activated), this, [this, insertToken](int index){ insertToken(m_videoOutputTemplateInput, m_videoTemplateTokensCombo, index); });
    connect(m_saveAudioTemplateButton, &QPushButton::clicked, this, &OutputTemplatesPage::validateAndSaveAudioTemplate);
    connect(m_audioTemplateTokensCombo, QOverload<int>::of(&QComboBox::activated), this, [this, insertToken](int index){ insertToken(m_audioOutputTemplateInput, m_audioTemplateTokensCombo, index); });
    connect(m_saveGalleryDlTemplateButton, &QPushButton::clicked, this, &OutputTemplatesPage::validateAndSaveGalleryDlTemplate);
    connect(m_galleryDlTemplateTokensCombo, QOverload<int>::of(&QComboBox::activated), this, [this, insertToken](int index){ insertToken(m_galleryDlOutputTemplateInput, m_galleryDlTemplateTokensCombo, index); });
    connect(m_configManager, &ConfigManager::settingChanged, this, &OutputTemplatesPage::handleConfigSettingChanged);
}

void OutputTemplatesPage::loadSettings() {
    QSignalBlocker b1(m_videoOutputTemplateInput);
    QSignalBlocker b2(m_audioOutputTemplateInput);
    QSignalBlocker b3(m_galleryDlOutputTemplateInput);

    QString defaultTpl = m_configManager->get("General", "output_template").toString();

    QString videoTpl = m_configManager->get("General", "output_template_video").toString();
    m_videoOutputTemplateInput->setText(videoTpl.isEmpty() ? defaultTpl : videoTpl);

    QString audioTpl = m_configManager->get("General", "output_template_audio").toString();
    m_audioOutputTemplateInput->setText(audioTpl.isEmpty() ? defaultTpl : audioTpl);

    m_galleryDlOutputTemplateInput->setText(m_configManager->get("General", "gallery_output_template").toString());
}

void OutputTemplatesPage::validateAndSaveVideoTemplate() {
    QString templateStr = m_videoOutputTemplateInput->text();
    if (templateStr.isEmpty()) { QMessageBox::warning(this, "Invalid Template", "Template cannot be empty."); return; }

    if (!validateYtDlpTemplate(this, m_configManager, templateStr)) {
        return;
    }

    m_configManager->set("General", "output_template_video", templateStr);
    QMessageBox::information(this, "Saved", "Video output filename pattern saved.");
}

void OutputTemplatesPage::validateAndSaveAudioTemplate() {
    QString templateStr = m_audioOutputTemplateInput->text();
    if (templateStr.isEmpty()) { QMessageBox::warning(this, "Invalid Template", "Template cannot be empty."); return; }

    if (!validateYtDlpTemplate(this, m_configManager, templateStr)) {
        return;
    }

    m_configManager->set("General", "output_template_audio", templateStr);
    QMessageBox::information(this, "Saved", "Audio output filename pattern saved.");
}

void OutputTemplatesPage::validateAndSaveGalleryDlTemplate() {
    QString templateStr = m_galleryDlOutputTemplateInput->text();
    if (templateStr.isEmpty()) { QMessageBox::warning(this, "Invalid Template", "Template cannot be empty."); return; }
    m_configManager->set("General", "gallery_output_template", templateStr);
    QMessageBox::information(this, "Saved", "Gallery output filename pattern saved.");
}

void OutputTemplatesPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    Q_UNUSED(value);
    if (section == "General" && (
        key == "output_template_video" ||
        key == "output_template_audio" ||
        key == "gallery_output_template" ||
        key == "output_template"
    )) {
        loadSettings();
    }
}