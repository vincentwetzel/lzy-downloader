#include "SubtitlesPage.h"
#include "core/ConfigManager.h"
#include "ui/ToggleSwitch.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QMap>
#include <QSignalBlocker>
#include <algorithm>

SubtitlesPage::SubtitlesPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *subtitlesGroup = new QGroupBox("Subtitles", this);
    subtitlesGroup->setToolTip("Configure how subtitles are downloaded and handled.");
    QFormLayout *subtitlesLayout = new QFormLayout(subtitlesGroup);

    m_subtitleLanguagesDisplay = new QLineEdit(this);
    m_subtitleLanguagesDisplay->setReadOnly(true);
    m_subtitleLanguagesDisplay->setToolTip("Comma-separated list of subtitle language codes to download (e.g., en,es,fr).");
    m_selectLanguagesButton = new QPushButton("Select...", this);
    m_selectLanguagesButton->setToolTip("Open a dialog to easily select which subtitle languages to download.");
    QHBoxLayout *langLayout = new QHBoxLayout();
    langLayout->addWidget(m_subtitleLanguagesDisplay, 1);
    langLayout->addWidget(m_selectLanguagesButton);

    m_embedSubtitlesCheck = new ToggleSwitch(this);
    m_embedSubtitlesCheck->setToolTip("Embed downloaded subtitles directly into the video file (requires compatible container like MKV or MP4).");
    m_writeSubtitlesCheck = new ToggleSwitch(this);
    m_writeSubtitlesCheck->setToolTip("Save subtitles as separate files alongside the video.");
    m_includeAutoSubtitlesCheck = new ToggleSwitch(this);
    m_includeAutoSubtitlesCheck->setToolTip("Include auto-generated subtitles (e.g., YouTube's auto-captions) if manual ones aren't available.");
    m_subtitleFormatCombo = new QComboBox(this);
    m_subtitleFormatCombo->setToolTip("Select the preferred format for downloaded subtitle files.");
    m_subtitleFormatCombo->addItems({"srt", "vtt", "ass"});

    auto addFormRow = [&](const QString& labelText, QWidget* field) {
        QLabel* label = new QLabel(labelText, this);
        label->setToolTip(field->toolTip());
        subtitlesLayout->addRow(label, field);
    };

    QLabel *langLabel = new QLabel("Subtitle language(s):", this);
    langLabel->setToolTip(m_subtitleLanguagesDisplay->toolTip());
    subtitlesLayout->addRow(langLabel, langLayout);
    addFormRow("Embed subtitles in video", m_embedSubtitlesCheck);
    addFormRow("Write subtitles to separate file(s)", m_writeSubtitlesCheck);
    addFormRow("Include automatically-generated subtitles", m_includeAutoSubtitlesCheck);
    addFormRow("Subtitle file format:", m_subtitleFormatCombo);

    layout->addWidget(subtitlesGroup);
    layout->addStretch();

    connect(m_selectLanguagesButton, &QPushButton::clicked, this, &SubtitlesPage::onSelectLanguagesClicked);
    connect(m_embedSubtitlesCheck, &ToggleSwitch::toggled, this, &SubtitlesPage::onEmbedSubtitlesToggled);
    connect(m_writeSubtitlesCheck, &ToggleSwitch::toggled, this, &SubtitlesPage::onWriteSubtitlesToggled);
    connect(m_includeAutoSubtitlesCheck, &ToggleSwitch::toggled, this, &SubtitlesPage::onIncludeAutoSubtitlesToggled);
    connect(m_subtitleFormatCombo, &QComboBox::currentTextChanged, this, &SubtitlesPage::onSubtitleFormatChanged);
    connect(m_configManager, &ConfigManager::settingChanged, this, &SubtitlesPage::handleConfigSettingChanged);
}

void SubtitlesPage::loadSettings() {
    QSignalBlocker b1(m_subtitleLanguagesDisplay);
    QSignalBlocker b2(m_selectLanguagesButton);
    QSignalBlocker b3(m_embedSubtitlesCheck);
    QSignalBlocker b4(m_writeSubtitlesCheck);
    QSignalBlocker b5(m_includeAutoSubtitlesCheck);
    QSignalBlocker b6(m_subtitleFormatCombo);

    m_subtitleLanguagesDisplay->setText(m_configManager->get("Subtitles", "languages", "en").toString());
    m_embedSubtitlesCheck->setChecked(m_configManager->get("Subtitles", "embed_subtitles", true).toBool());
    m_writeSubtitlesCheck->setChecked(m_configManager->get("Subtitles", "write_subtitles", false).toBool());
    m_includeAutoSubtitlesCheck->setChecked(m_configManager->get("Subtitles", "write_auto_subtitles", true).toBool());
    m_subtitleFormatCombo->setCurrentText(m_configManager->get("Subtitles", "format", "srt").toString());
    updateSubtitleFormatAvailability(m_embedSubtitlesCheck->isChecked());
}

void SubtitlesPage::onSelectLanguagesClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Select Subtitle Languages");
    dialog.setMinimumWidth(400);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QListWidget *listWidget = new QListWidget(&dialog);
    layout->addWidget(listWidget);

    QMap<QString, QString> langMap;
    langMap["runtime"] = "Select at Runtime";
    langMap["all"] = "All available";
    langMap["auto"] = "Auto-generated";
    langMap["live_chat"] = "Live Chat";
    langMap["en"] = "English"; langMap["es"] = "Spanish"; langMap["fr"] = "French"; langMap["de"] = "German";
    langMap["it"] = "Italian"; langMap["pt"] = "Portuguese"; langMap["ru"] = "Russian"; langMap["ja"] = "Japanese";
    langMap["ko"] = "Korean"; langMap["zh-Hans"] = "Chinese (Simplified)"; langMap["zh-Hant"] = "Chinese (Traditional)";
    langMap["ar"] = "Arabic"; langMap["hi"] = "Hindi"; langMap["bn"] = "Bengali"; langMap["pa"] = "Punjabi";
    langMap["tr"] = "Turkish"; langMap["vi"] = "Vietnamese"; langMap["id"] = "Indonesian"; langMap["nl"] = "Dutch";
    langMap["pl"] = "Polish"; langMap["sv"] = "Swedish"; langMap["fi"] = "Finnish"; langMap["no"] = "Norwegian";
    langMap["da"] = "Danish"; langMap["el"] = "Greek"; langMap["cs"] = "Czech"; langMap["hu"] = "Hungarian";
    langMap["ro"] = "Romanian"; langMap["uk"] = "Ukrainian"; langMap["th"] = "Thai";

    QStringList keys = langMap.keys();
    std::sort(keys.begin(), keys.end(), [&langMap](const QString &a, const QString &b){
        auto rank = [](const QString &k) {
            if (k == "runtime") return 0;
            if (k == "all") return 1;
            if (k == "auto") return 2;
            if (k == "live_chat") return 3;
            return 4;
        };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return langMap[a] < langMap[b];
    });

    QStringList selectedLangs = m_subtitleLanguagesDisplay->text().split(',', Qt::SkipEmptyParts);

    for (const QString& sel : selectedLangs) {
        QString code = sel.trimmed();
        if (!code.isEmpty() && !keys.contains(code)) {
            keys.append(code);
            langMap[code] = QString("Custom (%1)").arg(code);
        }
    }

    for (const QString& code : keys) {
        QListWidgetItem *item = new QListWidgetItem(QString("%1 (%2)").arg(langMap[code], code), listWidget);
        item->setData(Qt::UserRole, code);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selectedLangs.contains(code) ? Qt::Checked : Qt::Unchecked);
    }

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QStringList newSelectedLangs;
        for (int i = 0; i < listWidget->count(); ++i) {
            QListWidgetItem *item = listWidget->item(i);
            if (item->checkState() == Qt::Checked) {
                newSelectedLangs.append(item->data(Qt::UserRole).toString());
            }
        }
        QString langsStr = newSelectedLangs.join(',');
        m_subtitleLanguagesDisplay->setText(langsStr);
        m_configManager->set("Subtitles", "languages", langsStr);
    }
}

void SubtitlesPage::onEmbedSubtitlesToggled(bool c) { m_configManager->set("Subtitles", "embed_subtitles", c); }
void SubtitlesPage::onWriteSubtitlesToggled(bool c) { m_configManager->set("Subtitles", "write_subtitles", c); }
void SubtitlesPage::onIncludeAutoSubtitlesToggled(bool c) { m_configManager->set("Subtitles", "write_auto_subtitles", c); }
void SubtitlesPage::onSubtitleFormatChanged(const QString &text) { m_configManager->set("Subtitles", "format", text); }
void SubtitlesPage::updateSubtitleFormatAvailability(bool embedSubtitlesChecked) { m_subtitleFormatCombo->setDisabled(embedSubtitlesChecked); }
void SubtitlesPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section != "Subtitles") {
        return;
    }

    if (key == "languages") {
        m_subtitleLanguagesDisplay->setText(value.toString());
    } else if (key == "embed_subtitles") {
        QSignalBlocker b(m_embedSubtitlesCheck);
        const bool checked = value.toBool();
        m_embedSubtitlesCheck->setChecked(checked);
        updateSubtitleFormatAvailability(checked);
    } else if (key == "write_subtitles") {
        QSignalBlocker b(m_writeSubtitlesCheck);
        m_writeSubtitlesCheck->setChecked(value.toBool());
    } else if (key == "write_auto_subtitles") {
        QSignalBlocker b(m_includeAutoSubtitlesCheck);
        m_includeAutoSubtitlesCheck->setChecked(value.toBool());
    } else if (key == "format") {
        QSignalBlocker b(m_subtitleFormatCombo);
        m_subtitleFormatCombo->setCurrentText(value.toString());
    }
}
