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
        QString ytDlpPath = ProcessUtils::findBinary(QStringLiteral("yt-dlp"), configManager).path;
        if (ytDlpPath.isEmpty()) {
            QMessageBox::warning(parent, QObject::tr("Missing Binary"), QObject::tr("yt-dlp executable not found. Cannot validate template."));
            return false;
        }

        QProcess process;
        ProcessUtils::setProcessEnvironment(process);
        process.start(ytDlpPath, QStringList() << QStringLiteral("-o") << templateStr << QStringLiteral("dummy:"));
        if (!process.waitForStarted(2000)) {
            QMessageBox::warning(parent, QObject::tr("Process Error"), QObject::tr("Failed to start yt-dlp to validate the template. Please check your yt-dlp installation."));
            return false;
        }
        if (!process.waitForFinished(5000)) {
            ProcessUtils::terminateProcessTree(&process);
            QMessageBox::warning(parent, QObject::tr("Validation Timeout"), QObject::tr("yt-dlp took too long to validate the template (exceeded 5 seconds)."));
            return false;
        }
        QString err = process.readAllStandardError();
        if (err.contains(QStringLiteral("error:"), Qt::CaseInsensitive) && (err.contains(QStringLiteral("template"), Qt::CaseInsensitive) || err.contains(QStringLiteral("missing"), Qt::CaseInsensitive))) {
            QMessageBox::warning(parent, QObject::tr("Invalid Template"), QObject::tr("yt-dlp rejected the template:\n%1").arg(err.trimmed()));
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
        QStringLiteral("%(title)s"), QStringLiteral("%(uploader)s"), QStringLiteral("%(upload_date>%Y-%m-%d)s"), QStringLiteral("%(id)s"), QStringLiteral("%(ext)s"),
        QStringLiteral("%(playlist_index)s"), QStringLiteral("%(playlist_title)s"), QStringLiteral("%(channel)s"), QStringLiteral("%(channel_id)s"),
        QStringLiteral("%(duration)s"), QStringLiteral("%(view_count)s"), QStringLiteral("%(like_count)s"), QStringLiteral("%(comment_count)s"),
        QStringLiteral("%(age_limit)s"), QStringLiteral("%(genre)s"), QStringLiteral("%(format_id)s"), QStringLiteral("%(format)s"), QStringLiteral("%(format_note)s"),
        QStringLiteral("%(resolution)s"), QStringLiteral("%(width)s"), QStringLiteral("%(height)s"), QStringLiteral("%(fps)s"), QStringLiteral("%(vcodec)s"), QStringLiteral("%(acodec)s"),
        QStringLiteral("%(abr)s"), QStringLiteral("%(vbr)s"), QStringLiteral("%(tbr)s"), QStringLiteral("%(filesize)s"), QStringLiteral("%(epoch)s"), QStringLiteral("%(autonumber)s")
    };

    QGroupBox *ytDlpGroup = new QGroupBox(tr("yt-dlp Filename Templates"), this);
    ytDlpGroup->setToolTip(tr("Define output filename patterns for video and audio downloads using yt-dlp template fields."));
    QGridLayout *ytDlpLayout = new QGridLayout(ytDlpGroup);

    QLabel* videoPatternLabel = new QLabel(tr("Video Pattern:"), this);
    videoPatternLabel->setToolTip(tr("Template used to name video downloads. Include %(ext)s so yt-dlp can set the final extension."));
    ytDlpLayout->addWidget(videoPatternLabel, 0, 0);
    m_videoOutputTemplateInput = new QLineEdit(this);
    m_videoOutputTemplateInput->setMinimumHeight(30);
    m_videoOutputTemplateInput->setToolTip(tr("Template used to name video downloads. Example: %(title)s [%(id)s].%(ext)s"));
    ytDlpLayout->addWidget(m_videoOutputTemplateInput, 0, 1);
    m_videoTemplateTokensCombo = new QComboBox(this);
    m_videoTemplateTokensCombo->addItem(tr("Insert token..."), QStringLiteral(""));
    m_videoTemplateTokensCombo->setMaximumWidth(180);
    m_videoTemplateTokensCombo->setToolTip(tr("Insert a yt-dlp metadata field at the cursor position in the video template."));
    m_videoTemplateTokensCombo->addItems(ytDlpTokens);
    ytDlpLayout->addWidget(m_videoTemplateTokensCombo, 0, 2);
    m_saveVideoTemplateButton = new QPushButton(tr("Save"), this);
    m_saveVideoTemplateButton->setToolTip(tr("Validate this video template with yt-dlp, then save it if it is valid."));
    ytDlpLayout->addWidget(m_saveVideoTemplateButton, 0, 3);
    QPushButton *resetVideoButton = new QPushButton(tr("Reset"), this);
    resetVideoButton->setToolTip(tr("Reset the video template to its default value."));
    ytDlpLayout->addWidget(resetVideoButton, 0, 4);

    QLabel* audioPatternLabel = new QLabel(tr("Audio Pattern:"), this);
    audioPatternLabel->setToolTip(tr("Template used to name audio downloads. Include %(ext)s so yt-dlp can set the final extension."));
    ytDlpLayout->addWidget(audioPatternLabel, 1, 0);
    m_audioOutputTemplateInput = new QLineEdit(this);
    m_audioOutputTemplateInput->setMinimumHeight(30);
    m_audioOutputTemplateInput->setToolTip(tr("Template used to name audio downloads. Example: %(playlist_index)s - %(title)s.%(ext)s"));
    ytDlpLayout->addWidget(m_audioOutputTemplateInput, 1, 1);
    m_audioTemplateTokensCombo = new QComboBox(this);
    m_audioTemplateTokensCombo->addItem(tr("Insert token..."), QStringLiteral(""));
    m_audioTemplateTokensCombo->setMaximumWidth(180);
    m_audioTemplateTokensCombo->setToolTip(tr("Insert a yt-dlp metadata field at the cursor position in the audio template."));
    m_audioTemplateTokensCombo->addItems(ytDlpTokens);
    ytDlpLayout->addWidget(m_audioTemplateTokensCombo, 1, 2);
    m_saveAudioTemplateButton = new QPushButton(tr("Save"), this);
    m_saveAudioTemplateButton->setToolTip(tr("Validate this audio template with yt-dlp, then save it if it is valid."));
    ytDlpLayout->addWidget(m_saveAudioTemplateButton, 1, 3);
    QPushButton *resetAudioButton = new QPushButton(tr("Reset"), this);
    resetAudioButton->setToolTip(tr("Reset the audio template to its default value."));
    ytDlpLayout->addWidget(resetAudioButton, 1, 4);
    layout->addWidget(ytDlpGroup);

    QGroupBox *galleryDlGroup = new QGroupBox(tr("gallery-dl Filename Template"), this);
    galleryDlGroup->setToolTip(tr("Define output paths and filenames for gallery downloads using gallery-dl template fields."));
    QHBoxLayout *galleryDlControlsLayout = new QHBoxLayout(galleryDlGroup);
    QLabel* galleryPatternLabel = new QLabel(tr("Gallery Pattern:"), this);
    galleryPatternLabel->setToolTip(tr("Template used to name gallery downloads. It may include folders, such as {category}/{id}_{filename}.{extension}."));
    galleryDlControlsLayout->addWidget(galleryPatternLabel);
    m_galleryDlOutputTemplateInput = new QLineEdit(this);
    m_galleryDlOutputTemplateInput->setMinimumHeight(30);
    m_galleryDlOutputTemplateInput->setToolTip(tr("Template used to name gallery downloads. Include {extension} so gallery-dl can keep the correct file type."));
    galleryDlControlsLayout->addWidget(m_galleryDlOutputTemplateInput, 1); // Stretch factor 1
    m_galleryDlTemplateTokensCombo = new QComboBox(this);
    m_galleryDlTemplateTokensCombo->addItem(tr("Insert token..."), QStringLiteral(""));
    m_galleryDlTemplateTokensCombo->setMaximumWidth(180);
    m_galleryDlTemplateTokensCombo->setToolTip(tr("Insert a gallery-dl metadata field at the cursor position in the gallery template."));
    m_galleryDlTemplateTokensCombo->addItems({
        QStringLiteral("{category}"), QStringLiteral("{subcategory}"), QStringLiteral("{id}"), QStringLiteral("{filename}"), QStringLiteral("{extension}"),
        QStringLiteral("{title}"), QStringLiteral("{description}"), QStringLiteral("{date}"), QStringLiteral("{date:%Y-%m-%d}"),
        QStringLiteral("{user[username]}"), QStringLiteral("{user[name]}"), QStringLiteral("{user[id]}"),
        QStringLiteral("{author[name]}"), QStringLiteral("{author[url]}"), QStringLiteral("{author[id]}"),
        QStringLiteral("{url}"), QStringLiteral("{shortcode}"), QStringLiteral("{num}"), QStringLiteral("{count}"),
        QStringLiteral("{width}"), QStringLiteral("{height}"), QStringLiteral("{size}"), QStringLiteral("{width}x{height}"),
        QStringLiteral("{post[title]}"), QStringLiteral("{post[id]}"), QStringLiteral("{post[num]}"), QStringLiteral("{post[count]}"),
        QStringLiteral("{media[num]}"), QStringLiteral("{media[count]}"),
        QStringLiteral("{category}/{id}_{filename}.{extension}")
    });
    galleryDlControlsLayout->addWidget(m_galleryDlTemplateTokensCombo, 0); // No stretch
    m_saveGalleryDlTemplateButton = new QPushButton(tr("Save"), this);
    m_saveGalleryDlTemplateButton->setToolTip(tr("Save the gallery template. gallery-dl will validate fields when a gallery download runs."));
    galleryDlControlsLayout->addWidget(m_saveGalleryDlTemplateButton);
    QPushButton *resetGalleryDlButton = new QPushButton(tr("Reset"), this);
    resetGalleryDlButton->setToolTip(tr("Reset the gallery template to its default value."));
    galleryDlControlsLayout->addWidget(resetGalleryDlButton);
    layout->addWidget(galleryDlGroup);

    layout->addStretch();

    connect(resetVideoButton, &QPushButton::clicked, this, [this]() {
        m_videoOutputTemplateInput->setText(m_configManager->get(QStringLiteral("General"), QStringLiteral("output_template")).toString());
        QMessageBox::information(this, tr("Template Reset"), tr("Video filename pattern has been reset to default."));
    });
    connect(resetAudioButton, &QPushButton::clicked, this, [this]() {
        m_audioOutputTemplateInput->setText(m_configManager->get(QStringLiteral("General"), QStringLiteral("output_template")).toString());
        QMessageBox::information(this, tr("Template Reset"), tr("Audio filename pattern has been reset to default."));
    });
    connect(resetGalleryDlButton, &QPushButton::clicked, this, [this]() {
        QString defaultTpl = m_configManager->getDefault(QStringLiteral("General"), QStringLiteral("gallery_output_template")).toString();
        m_galleryDlOutputTemplateInput->setText(defaultTpl);
        QMessageBox::information(this, tr("Template Reset"), tr("Gallery filename pattern has been reset to default."));
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

    QString defaultTpl = m_configManager->get(QStringLiteral("General"), QStringLiteral("output_template")).toString();

    QString videoTpl = m_configManager->get(QStringLiteral("General"), QStringLiteral("output_template_video")).toString();
    m_videoOutputTemplateInput->setText(videoTpl.isEmpty() ? defaultTpl : videoTpl);

    QString audioTpl = m_configManager->get(QStringLiteral("General"), QStringLiteral("output_template_audio")).toString();
    m_audioOutputTemplateInput->setText(audioTpl.isEmpty() ? defaultTpl : audioTpl);

    m_galleryDlOutputTemplateInput->setText(m_configManager->get(QStringLiteral("General"), QStringLiteral("gallery_output_template")).toString());
}

void OutputTemplatesPage::validateAndSaveVideoTemplate() {
    QString templateStr = m_videoOutputTemplateInput->text();
    if (templateStr.isEmpty()) { QMessageBox::warning(this, tr("Invalid Template"), tr("Template cannot be empty.")); return; }

    if (!validateYtDlpTemplate(this, m_configManager, templateStr)) {
        return;
    }

    m_configManager->set(QStringLiteral("General"), QStringLiteral("output_template_video"), templateStr);
    QMessageBox::information(this, tr("Saved"), tr("Video output filename pattern saved."));
}

void OutputTemplatesPage::validateAndSaveAudioTemplate() {
    QString templateStr = m_audioOutputTemplateInput->text();
    if (templateStr.isEmpty()) { QMessageBox::warning(this, tr("Invalid Template"), tr("Template cannot be empty.")); return; }

    if (!validateYtDlpTemplate(this, m_configManager, templateStr)) {
        return;
    }

    m_configManager->set(QStringLiteral("General"), QStringLiteral("output_template_audio"), templateStr);
    QMessageBox::information(this, tr("Saved"), tr("Audio output filename pattern saved."));
}

void OutputTemplatesPage::validateAndSaveGalleryDlTemplate() {
    QString templateStr = m_galleryDlOutputTemplateInput->text();
    if (templateStr.isEmpty()) { QMessageBox::warning(this, tr("Invalid Template"), tr("Template cannot be empty.")); return; }
    m_configManager->set(QStringLiteral("General"), QStringLiteral("gallery_output_template"), templateStr);
    QMessageBox::information(this, tr("Saved"), tr("Gallery output filename pattern saved."));
}

void OutputTemplatesPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    Q_UNUSED(value);
    if (section == QStringLiteral("General") && (
        key == QStringLiteral("output_template_video") ||
        key == QStringLiteral("output_template_audio") ||
        key == QStringLiteral("gallery_output_template") ||
        key == QStringLiteral("output_template")
    )) {
        loadSettings();
    }
}
