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

    QGroupBox *subtitlesGroup = new QGroupBox(tr("Subtitles"), this);
    subtitlesGroup->setToolTip(tr("Choose subtitle languages and whether subtitles are embedded, written as separate files, or sourced from auto-captions."));
    QFormLayout *subtitlesLayout = new QFormLayout(subtitlesGroup);

    m_subtitleLanguagesDisplay = new QLineEdit(this);
    m_subtitleLanguagesDisplay->setReadOnly(true);
    m_subtitleLanguagesDisplay->setToolTip(tr("Language codes requested for subtitles, such as en, es, or fr. Use Select to choose common languages or runtime selection."));
    m_selectLanguagesButton = new QPushButton(tr("Select..."), this);
    m_selectLanguagesButton->setToolTip(tr("Open a language picker for common subtitle choices, including runtime selection and all available subtitles."));
    QHBoxLayout *langLayout = new QHBoxLayout();
    langLayout->addWidget(m_subtitleLanguagesDisplay, 1);
    langLayout->addWidget(m_selectLanguagesButton);

    m_embedSubtitlesCheck = new ToggleSwitch(this);
    m_embedSubtitlesCheck->setToolTip(tr("Embed subtitles into the final video file when the container supports it. When enabled, the separate subtitle file format is chosen by the embed process."));
    m_writeSubtitlesCheck = new ToggleSwitch(this);
    m_writeSubtitlesCheck->setToolTip(tr("Save subtitle files beside the downloaded video, even when subtitles are also embedded."));
    m_includeAutoSubtitlesCheck = new ToggleSwitch(this);
    m_includeAutoSubtitlesCheck->setToolTip(tr("Also allow machine-generated captions, such as YouTube auto-captions, when manual subtitles are missing."));
    m_subtitleFormatCombo = new QComboBox(this);
    m_subtitleFormatCombo->setToolTip(tr("Choose the preferred format for separate subtitle files. Disabled while subtitle embedding controls the format."));
    m_subtitleFormatCombo->addItems({QStringLiteral("srt"), QStringLiteral("vtt"), QStringLiteral("ass")});

    auto addFormRow = [&](const QString& labelText, QWidget* field) {
        QLabel* label = new QLabel(labelText, this);
        label->setToolTip(field->toolTip());
        subtitlesLayout->addRow(label, field);
    };

    QLabel *langLabel = new QLabel(tr("Subtitle language(s):"), this);
    langLabel->setToolTip(m_subtitleLanguagesDisplay->toolTip());
    subtitlesLayout->addRow(langLabel, langLayout);
    addFormRow(tr("Embed subtitles in video"), m_embedSubtitlesCheck);
    addFormRow(tr("Write subtitles to separate file(s)"), m_writeSubtitlesCheck);
    addFormRow(tr("Include automatically-generated subtitles"), m_includeAutoSubtitlesCheck);
    addFormRow(tr("Subtitle file format:"), m_subtitleFormatCombo);

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

    m_subtitleLanguagesDisplay->setText(m_configManager->get(QStringLiteral("Subtitles"), QStringLiteral("languages"), QStringLiteral("en")).toString());
    m_embedSubtitlesCheck->setChecked(m_configManager->get(QStringLiteral("Subtitles"), QStringLiteral("embed_subtitles"), true).toBool());
    m_writeSubtitlesCheck->setChecked(m_configManager->get(QStringLiteral("Subtitles"), QStringLiteral("write_subtitles"), false).toBool());
    m_includeAutoSubtitlesCheck->setChecked(m_configManager->get(QStringLiteral("Subtitles"), QStringLiteral("write_auto_subtitles"), true).toBool());
    m_subtitleFormatCombo->setCurrentText(m_configManager->get(QStringLiteral("Subtitles"), QStringLiteral("format"), QStringLiteral("srt")).toString());
    updateSubtitleFormatAvailability(m_embedSubtitlesCheck->isChecked());
}

void SubtitlesPage::onSelectLanguagesClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Select Subtitle Languages"));
    dialog.setMinimumWidth(400);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QListWidget *listWidget = new QListWidget(&dialog);
    layout->addWidget(listWidget);

    QMap<QString, QString> langMap;
    langMap[QStringLiteral("runtime")] = tr("Select at Runtime");
    langMap[QStringLiteral("all")] = tr("All available");
    langMap[QStringLiteral("auto")] = tr("Auto-generated");
    langMap[QStringLiteral("live_chat")] = tr("Live Chat");
    langMap[QStringLiteral("en")] = tr("English"); langMap[QStringLiteral("es")] = tr("Spanish"); langMap[QStringLiteral("fr")] = tr("French"); langMap[QStringLiteral("de")] = tr("German");
    langMap[QStringLiteral("it")] = tr("Italian"); langMap[QStringLiteral("pt")] = tr("Portuguese"); langMap[QStringLiteral("ru")] = tr("Russian"); langMap[QStringLiteral("ja")] = tr("Japanese");
    langMap[QStringLiteral("ko")] = tr("Korean"); langMap[QStringLiteral("zh-Hans")] = tr("Chinese (Simplified)"); langMap[QStringLiteral("zh-Hant")] = tr("Chinese (Traditional)");
    langMap[QStringLiteral("ar")] = tr("Arabic"); langMap[QStringLiteral("hi")] = tr("Hindi"); langMap[QStringLiteral("bn")] = tr("Bengali"); langMap[QStringLiteral("pa")] = tr("Punjabi");
    langMap[QStringLiteral("tr")] = tr("Turkish"); langMap[QStringLiteral("vi")] = tr("Vietnamese"); langMap[QStringLiteral("id")] = tr("Indonesian"); langMap[QStringLiteral("nl")] = tr("Dutch");
    langMap[QStringLiteral("pl")] = tr("Polish"); langMap[QStringLiteral("sv")] = tr("Swedish"); langMap[QStringLiteral("fi")] = tr("Finnish"); langMap[QStringLiteral("no")] = tr("Norwegian");
    langMap[QStringLiteral("da")] = tr("Danish"); langMap[QStringLiteral("el")] = tr("Greek"); langMap[QStringLiteral("cs")] = tr("Czech"); langMap[QStringLiteral("hu")] = tr("Hungarian");
    langMap[QStringLiteral("ro")] = tr("Romanian"); langMap[QStringLiteral("uk")] = tr("Ukrainian"); langMap[QStringLiteral("th")] = tr("Thai");

    QStringList keys = langMap.keys();
    std::sort(keys.begin(), keys.end(), [&langMap](const QString &a, const QString &b){
        auto rank = [](const QString &k) {
            if (k == QStringLiteral("runtime")) return 0;
            if (k == QStringLiteral("all")) return 1;
            if (k == QStringLiteral("auto")) return 2;
            if (k == QStringLiteral("live_chat")) return 3;
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
            langMap[code] = tr("Custom (%1)").arg(code);
        }
    }

    for (const QString& code : keys) {
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(langMap[code], code), listWidget);
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
        m_configManager->set(QStringLiteral("Subtitles"), QStringLiteral("languages"), langsStr);
    }
}

void SubtitlesPage::onEmbedSubtitlesToggled(bool c) { m_configManager->set(QStringLiteral("Subtitles"), QStringLiteral("embed_subtitles"), c); }
void SubtitlesPage::onWriteSubtitlesToggled(bool c) { m_configManager->set(QStringLiteral("Subtitles"), QStringLiteral("write_subtitles"), c); }
void SubtitlesPage::onIncludeAutoSubtitlesToggled(bool c) { m_configManager->set(QStringLiteral("Subtitles"), QStringLiteral("write_auto_subtitles"), c); }
void SubtitlesPage::onSubtitleFormatChanged(const QString &text) { m_configManager->set(QStringLiteral("Subtitles"), QStringLiteral("format"), text); }
void SubtitlesPage::updateSubtitleFormatAvailability(bool embedSubtitlesChecked) { m_subtitleFormatCombo->setDisabled(embedSubtitlesChecked); }
void SubtitlesPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section != QStringLiteral("Subtitles")) {
        return;
    }

    if (key == QStringLiteral("languages")) {
        m_subtitleLanguagesDisplay->setText(value.toString());
    } else if (key == QStringLiteral("embed_subtitles")) {
        QSignalBlocker b(m_embedSubtitlesCheck);
        const bool checked = value.toBool();
        m_embedSubtitlesCheck->setChecked(checked);
        updateSubtitleFormatAvailability(checked);
    } else if (key == QStringLiteral("write_subtitles")) {
        QSignalBlocker b(m_writeSubtitlesCheck);
        m_writeSubtitlesCheck->setChecked(value.toBool());
    } else if (key == QStringLiteral("write_auto_subtitles")) {
        QSignalBlocker b(m_includeAutoSubtitlesCheck);
        m_includeAutoSubtitlesCheck->setChecked(value.toBool());
    } else if (key == QStringLiteral("format")) {
        QSignalBlocker b(m_subtitleFormatCombo);
        m_subtitleFormatCombo->setCurrentText(value.toString());
    }
}
