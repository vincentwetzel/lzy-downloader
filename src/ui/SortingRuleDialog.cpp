#include "SortingRuleDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMenu>
#include <QTextEdit>
#include <algorithm> // For std::sort
#include <QStackedWidget>
#include <QLineEdit>
#include <QDir>
#include <QSizePolicy>
#include <QScrollArea>
#include <QComboBox>
#include <QPushButton>
#include <QRegularExpression>

// CONSTANT: Height for condition text entry boxes
static const int CONDITION_VALUE_INPUT_HEIGHT = 100;

// A simple widget for editing a single condition
class ConditionWidget : public QWidget {
public:
    ConditionWidget(QWidget *parent = nullptr) : QWidget(parent) {
        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(2);
        
        QHBoxLayout *topLayout = new QHBoxLayout();
        topLayout->setSpacing(4);

        m_fieldCombo = new QComboBox(this);
        m_fieldCombo->addItem(tr("Uploader"), QStringLiteral("Uploader"));
        m_fieldCombo->addItem(tr("Title"), QStringLiteral("Title"));
        m_fieldCombo->addItem(tr("Playlist Title"), QStringLiteral("Playlist Title"));
        m_fieldCombo->addItem(tr("Duration (seconds)"), QStringLiteral("Duration (seconds)"));
        m_fieldCombo->addItem(tr("Album"), QStringLiteral("Album"));
        m_fieldCombo->addItem(tr("ID"), QStringLiteral("ID"));
        m_fieldCombo->setToolTip(tr("Select the metadata field to examine."));
        m_fieldCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        m_operatorCombo = new QComboBox(this);
        m_operatorCombo->setToolTip(tr("Select the comparison operator."));
        m_operatorCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        // Items will be populated by onFieldChanged

        m_valueInputSingle = new QLineEdit(this);
        m_valueInputSingle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_valueInputSingle->setFixedHeight(CONDITION_VALUE_INPUT_HEIGHT);
        
        m_valueInputMulti = new QTextEdit(this);
        m_valueInputMulti->setAcceptRichText(false);
        m_valueInputMulti->setFixedHeight(CONDITION_VALUE_INPUT_HEIGHT);
        m_valueInputMulti->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_valueStackedWidget = new QStackedWidget(this);
        m_valueStackedWidget->addWidget(m_valueInputSingle);
        m_valueStackedWidget->addWidget(m_valueInputMulti);

        topLayout->addWidget(m_fieldCombo);
        topLayout->addWidget(m_operatorCombo);
        topLayout->addStretch();

        mainLayout->addLayout(topLayout);
        mainLayout->addWidget(m_valueStackedWidget);

        connect(m_fieldCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0) {
                onFieldChanged(m_fieldCombo->itemData(index).toString());
            }
        });
        connect(m_operatorCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0) {
                onOperatorChanged(m_operatorCombo->itemData(index).toString());
            }
        });

        // Initial state setup
        onFieldChanged(m_fieldCombo->currentData().toString());
    }

    [[nodiscard]] QVariantMap getCondition() const {
        QVariantMap condition;
        QString uiField = m_fieldCombo->currentData().toString();
        QString internalField = uiField.toLower();
        if (uiField == QStringLiteral("Playlist Title")) internalField = QStringLiteral("playlist_title");
        else if (uiField == QStringLiteral("Duration (seconds)")) internalField = QStringLiteral("duration");
        else if (uiField == QStringLiteral("Album")) internalField = QStringLiteral("album");
        else if (uiField == QStringLiteral("ID")) internalField = QStringLiteral("id");
        
        condition[QStringLiteral("field")] = internalField;
        condition[QStringLiteral("operator")] = m_operatorCombo->currentData().toString();
        if (m_operatorCombo->currentData().toString() == QStringLiteral("Is One Of")) {
            condition[QStringLiteral("value")] = m_valueInputMulti->toPlainText();
        } else {
            condition[QStringLiteral("value")] = m_valueInputSingle->text();
        }
        return condition;
    }

    void setCondition(const QVariantMap &condition) {
        QString field = condition[QStringLiteral("field")].toString();
        
        // Map internal fields back to UI fields
        if (field.compare(QStringLiteral("playlist_title"), Qt::CaseInsensitive) == 0 || field.compare(QStringLiteral("playlist"), Qt::CaseInsensitive) == 0) field = QStringLiteral("Playlist Title");
        else if (field.compare(QStringLiteral("duration"), Qt::CaseInsensitive) == 0) field = QStringLiteral("Duration (seconds)");
        else if (field.compare(QStringLiteral("album"), Qt::CaseInsensitive) == 0) field = QStringLiteral("Album");
        else if (field.compare(QStringLiteral("id"), Qt::CaseInsensitive) == 0) field = QStringLiteral("ID");
        else if (field.compare(QStringLiteral("uploader"), Qt::CaseInsensitive) == 0) field = QStringLiteral("Uploader");
        else if (field.compare(QStringLiteral("title"), Qt::CaseInsensitive) == 0) field = QStringLiteral("Title");
        
        // Fallback case-insensitive match
        for (int i = 0; i < m_fieldCombo->count(); ++i) {
            if (m_fieldCombo->itemData(i).toString().compare(field, Qt::CaseInsensitive) == 0) {
                field = m_fieldCombo->itemData(i).toString();
                break;
            }
        }
        
        m_fieldCombo->setCurrentIndex(m_fieldCombo->findData(field));
        onFieldChanged(field); // Set up operators

        QString op = condition[QStringLiteral("operator")].toString();
        if (op.compare(QStringLiteral("Equals"), Qt::CaseInsensitive) == 0) {
            op = QStringLiteral("Is");
        }
        for (int i = 0; i < m_operatorCombo->count(); ++i) {
            if (m_operatorCombo->itemData(i).toString().compare(op, Qt::CaseInsensitive) == 0) {
                op = m_operatorCombo->itemData(i).toString();
                break;
            }
        }
        
        // Temporarily unhook onOperatorChanged so we don't mess up text copying
        QSignalBlocker opBlock(m_operatorCombo);
        m_operatorCombo->setCurrentIndex(m_operatorCombo->findData(op));
        opBlock.unblock();

        // Set value directly without triggering operator change effects yet
        if (m_operatorCombo->currentData().toString() == QStringLiteral("Is One Of")) {
            m_valueInputMulti->setPlainText(condition[QStringLiteral("value")].toString());
        } else {
            m_valueInputSingle->setText(condition[QStringLiteral("value")].toString());
        }
        
        // Now invoke to update UI state (placeholders, stacked widget)
        onOperatorChanged(m_operatorCombo->currentData().toString());
    }

    [[nodiscard]] QString getValueText() const {
        if (m_operatorCombo->currentData().toString() == QStringLiteral("Is One Of")) {
            return m_valueInputMulti->toPlainText();
        }
        return m_valueInputSingle->text();
    }

    void setValueText(const QString &text) {
        if (m_operatorCombo->currentData().toString() == QStringLiteral("Is One Of")) {
            m_valueInputMulti->setPlainText(text);
        } else {
            m_valueInputSingle->setText(text);
        }
    }

    [[nodiscard]] QString getOperatorText() const {
        return m_operatorCombo->currentData().toString();
    }


private slots:
    void onFieldChanged(const QString &field) {
        QString currentOperator = m_operatorCombo->currentData().toString();
        m_operatorCombo->clear();
        if (field == QStringLiteral("Duration (seconds)")) {
            m_operatorCombo->addItem(tr("Is"), QStringLiteral("Is"));
            m_operatorCombo->addItem(tr("Greater Than"), QStringLiteral("Greater Than"));
            m_operatorCombo->addItem(tr("Less Than"), QStringLiteral("Less Than"));
        } else {
            m_operatorCombo->addItem(tr("Contains"), QStringLiteral("Contains"));
            m_operatorCombo->addItem(tr("Is"), QStringLiteral("Is"));
            m_operatorCombo->addItem(tr("Starts With"), QStringLiteral("Starts With"));
            m_operatorCombo->addItem(tr("Ends With"), QStringLiteral("Ends With"));
            m_operatorCombo->addItem(tr("Is One Of"), QStringLiteral("Is One Of"));
        }
        // Try to restore the previously selected operator if it's still valid
        int index = m_operatorCombo->findData(currentOperator);
        if (index != -1) {
            m_operatorCombo->setCurrentIndex(index);
        } else {
            m_operatorCombo->setCurrentIndex(0); // Select first available operator
        }
        onOperatorChanged(m_operatorCombo->currentData().toString());
    }

    void onOperatorChanged(const QString &op) {
        if (op == QStringLiteral("Is One Of")) {
            m_valueInputMulti->setPlaceholderText(tr("Enter one value per line."));
            m_valueInputMulti->setToolTip(tr("Enter one value per line. The condition will match if the field is an exact match to any of the lines."));
            
            // When switching from single-line, copy text
            if (m_valueStackedWidget->currentWidget() == m_valueInputSingle && !m_valueInputSingle->text().isEmpty()) {
                m_valueInputMulti->setPlainText(m_valueInputSingle->text());
            }
            
            m_valueStackedWidget->setCurrentWidget(m_valueInputMulti);
        } else {
            QString placeholder = tr("Enter value.");
            if (m_fieldCombo->currentData().toString() == QStringLiteral("Duration (seconds)")) {
                placeholder = tr("Enter a number (e.g., 300 for 5 minutes).");
            }
            m_valueInputSingle->setPlaceholderText(placeholder);
            m_valueInputSingle->setToolTip(tr("Enter the value to compare against."));

            // When switching from multi-line, copy the first line to the single-line input
            if (m_valueStackedWidget->currentWidget() == m_valueInputMulti) {
                QString firstLine = m_valueInputMulti->toPlainText().split(QLatin1Char('\n')).first();
                m_valueInputSingle->setText(firstLine);
            }
            m_valueStackedWidget->setCurrentWidget(m_valueInputSingle);
        }
    }

private:
    QComboBox *m_fieldCombo;
    QComboBox *m_operatorCombo;
    QStackedWidget *m_valueStackedWidget;
    QLineEdit *m_valueInputSingle;
    QTextEdit *m_valueInputMulti;
};

SortingRuleDialog::SortingRuleDialog(QWidget *parent) : QDialog(parent) {
    setupUI();
}

SortingRuleDialog::SortingRuleDialog(const QVariantMap &rule, QWidget *parent) : QDialog(parent) {
    setupUI();
    setRule(rule);
}

void SortingRuleDialog::setupUI() {
    setWindowTitle(tr("Sorting Rule"));
    setMinimumSize(650, 500);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *formLayout = new QFormLayout();

    m_ruleNameInput = new QLineEdit(this);
    m_ruleNameInput->setToolTip(tr("A descriptive name for this rule (e.g., 'Music Videos')."));
    formLayout->addRow(tr("Rule Name:"), m_ruleNameInput);

    QHBoxLayout *targetFolderLayout = new QHBoxLayout();
    m_targetFolderInput = new QLineEdit(this);
    m_targetFolderInput->setToolTip(tr("The main folder where matching files will be moved."));
    m_browseButton = new QPushButton(tr("Browse..."), this);
    m_browseButton->setToolTip(tr("Browse to select the target folder."));
    targetFolderLayout->addWidget(m_targetFolderInput);
    targetFolderLayout->addWidget(m_browseButton);
    formLayout->addRow(tr("Target Folder:"), targetFolderLayout);

    QHBoxLayout *subfolderLayout = new QHBoxLayout();
    m_subfolderPatternInput = new QLineEdit(this);
    m_subfolderPatternInput->setToolTip(tr("Optional. Create subfolders using media properties. Use the dropdown to insert placeholders."));

    m_tokenDropdown = new QComboBox(this);
    m_tokenDropdown->setToolTip(tr("Insert a placeholder into the subfolder pattern."));
    m_tokenDropdown->addItem(tr("Insert Token..."));
    QStringList tokens = {QStringLiteral("{title}"), QStringLiteral("{uploader}"), QStringLiteral("{id}"), QStringLiteral("{album}"), QStringLiteral("{upload_year}"), QStringLiteral("{upload_month}"), QStringLiteral("{upload_day}"), QStringLiteral("{playlist_title}")};
    m_tokenDropdown->addItems(tokens);

    subfolderLayout->addWidget(m_subfolderPatternInput);
    subfolderLayout->addWidget(m_tokenDropdown);
    formLayout->addRow(tr("Subfolder Pattern:"), subfolderLayout);

    m_appliesToDropdown = new QComboBox(this);
    m_appliesToDropdown->setToolTip(tr("Choose which types of downloads this rule should apply to."));
    m_appliesToDropdown->addItem(tr("All Downloads"), QStringLiteral("All Downloads"));
    m_appliesToDropdown->addItem(tr("Video Downloads"), QStringLiteral("Video Downloads"));
    m_appliesToDropdown->addItem(tr("Audio Downloads"), QStringLiteral("Audio Downloads"));
    m_appliesToDropdown->addItem(tr("Gallery Downloads"), QStringLiteral("Gallery Downloads"));
    m_appliesToDropdown->addItem(tr("Video Playlist Downloads"), QStringLiteral("Video Playlist Downloads"));
    m_appliesToDropdown->addItem(tr("Audio Playlist Downloads"), QStringLiteral("Audio Playlist Downloads"));
    formLayout->addRow(tr("Rule Applies to:"), m_appliesToDropdown);

    mainLayout->addLayout(formLayout);

    QHBoxLayout* conditionsHeaderLayout = new QHBoxLayout();
    QLabel *conditionsHeaderLabel = new QLabel(tr("Conditions (All Must Match):"));
    conditionsHeaderLabel->setToolTip(tr("A list of conditions that must all be true for this sorting rule to trigger."));
    conditionsHeaderLayout->addWidget(conditionsHeaderLabel);
    conditionsHeaderLayout->addStretch();
    m_addConditionButton = new QPushButton(tr("Add Condition"), this);
    m_addConditionButton->setToolTip(tr("Add a new condition to this rule. All conditions must be met for the rule to apply."));
    conditionsHeaderLayout->addWidget(m_addConditionButton);
    mainLayout->addLayout(conditionsHeaderLayout);

    // Use QScrollArea for smooth pixel-level scrolling instead of QListWidget's item-snapping
    m_conditionsScrollArea = new QScrollArea(this);
    m_conditionsScrollArea->setWidgetResizable(true);
    m_conditionsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_conditionsScrollArea->setMinimumHeight(150);
    m_conditionsScrollArea->setMaximumHeight(400);
    m_conditionsScrollArea->setToolTip(tr("Add one or more conditions. A download must match ALL of them for this rule to apply."));

    m_conditionsContainer = new QWidget();
    m_conditionsLayout = new QVBoxLayout(m_conditionsContainer);
    m_conditionsLayout->setContentsMargins(0, 0, 0, 0);
    m_conditionsLayout->setSpacing(4);
    m_conditionsLayout->addStretch(); // Push conditions to top

    m_conditionsScrollArea->setWidget(m_conditionsContainer);
    mainLayout->addWidget(m_conditionsScrollArea);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SortingRuleDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    connect(m_browseButton, &QPushButton::clicked, this, &SortingRuleDialog::browseTargetFolder);
    connect(m_addConditionButton, &QPushButton::clicked, this, [this]() { addCondition(); });
    connect(m_tokenDropdown, &QComboBox::activated, this, [this](int index){
        if (index > 0) {
            insertToken(m_tokenDropdown->itemText(index));
            m_tokenDropdown->setCurrentIndex(0);
        }
    });
}

void SortingRuleDialog::setRule(const QVariantMap &rule) {
    m_ruleNameInput->setText(rule[QStringLiteral("name")].toString());
    m_targetFolderInput->setText(QDir::toNativeSeparators(rule[QStringLiteral("target_folder")].toString()));
    m_subfolderPatternInput->setText(rule[QStringLiteral("subfolder_pattern")].toString());

    if (rule.contains(QStringLiteral("applies_to"))) {
        QString appliesTo = rule[QStringLiteral("applies_to")].toString();
        
        if (appliesTo.compare(QStringLiteral("video"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("Video Downloads");
        else if (appliesTo.compare(QStringLiteral("audio"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("Audio Downloads");
        else if (appliesTo.compare(QStringLiteral("gallery"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("Gallery Downloads");
        else if (appliesTo.compare(QStringLiteral("video_playlist"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("Video Playlist Downloads");
        else if (appliesTo.compare(QStringLiteral("audio_playlist"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("Audio Playlist Downloads");
        else if (appliesTo.compare(QStringLiteral("any"), Qt::CaseInsensitive) == 0 || appliesTo.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("All Downloads");
        
        const int idx = m_appliesToDropdown->findData(appliesTo);
        if (idx >= 0) {
            m_appliesToDropdown->setCurrentIndex(idx);
        }
    }

    // Clear existing conditions (remove all but the stretch)
    while (m_conditionsLayout->count() > 1) {
        QLayoutItem *item = m_conditionsLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QVariantList conditions = rule[QStringLiteral("conditions")].toList();
    for (const QVariant &condVariant : conditions) {
        addCondition(condVariant.toMap());
    }
}

QVariantMap SortingRuleDialog::getRule() const {
    QVariantMap rule;
    rule[QStringLiteral("name")] = m_ruleNameInput->text();
    rule[QStringLiteral("target_folder")] = QDir::cleanPath(m_targetFolderInput->text());
    rule[QStringLiteral("subfolder_pattern")] = m_subfolderPatternInput->text();

    QString uiAppliesTo = m_appliesToDropdown->currentData().toString();
    QString internalAppliesTo = QStringLiteral("any");
    if (uiAppliesTo == QStringLiteral("Video Downloads")) internalAppliesTo = QStringLiteral("video");
    else if (uiAppliesTo == QStringLiteral("Audio Downloads")) internalAppliesTo = QStringLiteral("audio");
    else if (uiAppliesTo == QStringLiteral("Gallery Downloads")) internalAppliesTo = QStringLiteral("gallery");
    else if (uiAppliesTo == QStringLiteral("Video Playlist Downloads")) internalAppliesTo = QStringLiteral("video_playlist");
    else if (uiAppliesTo == QStringLiteral("Audio Playlist Downloads")) internalAppliesTo = QStringLiteral("audio_playlist");
    rule[QStringLiteral("applies_to")] = internalAppliesTo;

    QVariantList conditions;
    // Iterate through layout containers (skip the stretch at the end)
    for (int i = 0; i < m_conditionsLayout->count() - 1; ++i) {
        QLayoutItem *item = m_conditionsLayout->itemAt(i);
        if (auto container = item->widget()) {
            for (QObject *child : container->children()) {
                if (auto conditionWidget = dynamic_cast<ConditionWidget*>(child)) {
                    conditions.append(conditionWidget->getCondition());
                    break;
                }
            }
        }
    }
    rule[QStringLiteral("conditions")] = conditions;
    return rule;
}

void SortingRuleDialog::browseTargetFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Target Folder"),
                                                    m_targetFolderInput->text(),
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        m_targetFolderInput->setText(QDir::toNativeSeparators(dir));
    }
}

void SortingRuleDialog::addCondition(const QVariantMap &condition) {
    QWidget *containerWidget = new QWidget(m_conditionsContainer);
    ConditionWidget *conditionWidget = new ConditionWidget(containerWidget);
    if (!condition.isEmpty()) {
        conditionWidget->setCondition(condition);
    }

    QPushButton *removeButton = new QPushButton(tr("Remove"));
    removeButton->setStyleSheet(QStringLiteral("color: #dc2626;"));
    removeButton->setToolTip(tr("Remove this condition."));
    removeButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QHBoxLayout *hbox = new QHBoxLayout();
    hbox->setContentsMargins(2, 2, 2, 2);
    hbox->setSpacing(4);
    hbox->addWidget(conditionWidget, 1);
    hbox->addWidget(removeButton);

    containerWidget->setLayout(hbox);

    // Insert before the stretch
    m_conditionsLayout->insertWidget(m_conditionsLayout->count() - 1, containerWidget);

    connect(removeButton, &QPushButton::clicked, this, [this, containerWidget]() {
        m_conditionsLayout->removeWidget(containerWidget);
        containerWidget->deleteLater();
    });
}

void SortingRuleDialog::insertToken(const QString &token) {
    m_subfolderPatternInput->insert(token);
}

void SortingRuleDialog::accept() {
    if (m_ruleNameInput->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Validation Error"), tr("Rule Name cannot be empty."));
        return;
    }
    if (m_targetFolderInput->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Validation Error"), tr("Target Folder cannot be empty."));
        return;
    }

    // Sort "Is One Of" values alphabetically
    for (int i = 0; i < m_conditionsLayout->count() - 1; ++i) {
        QLayoutItem *item = m_conditionsLayout->itemAt(i);
        if (auto container = item->widget()) {
            for (QObject *child : container->children()) {
                if (auto conditionWidget = dynamic_cast<ConditionWidget*>(child)) {
                    if (conditionWidget->getOperatorText() == QStringLiteral("Is One Of")) {
                        QStringList values = conditionWidget->getValueText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                        std::sort(values.begin(), values.end(), [](const QString &s1, const QString &s2) {
                            return s1.compare(s2, Qt::CaseInsensitive) < 0;
                        });
                        conditionWidget->setValueText(values.join(QLatin1Char('\n')));
                    }
                    break;
                }
            }
        }
    }

    QString subfolderPattern = m_subfolderPatternInput->text();
    QString error;
    if (!validateSubfolderPattern(subfolderPattern, error)) {
        QMessageBox::warning(this, tr("Invalid Subfolder Pattern"), error);
        return;
    }

    QDialog::accept();
}

bool SortingRuleDialog::validateSubfolderPattern(const QString &pattern, QString &error) const {
    static const QRegularExpression re(QStringLiteral("\\{([^}]+)\\}"));
    auto it = re.globalMatch(pattern);

    static const QSet<QString> validTokens = {
        QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("uploader"), QStringLiteral("uploader_id"), QStringLiteral("uploader_url"),
        QStringLiteral("upload_date"), QStringLiteral("license"), QStringLiteral("creator"), QStringLiteral("alt_title"), QStringLiteral("album"),
        QStringLiteral("display_id"), QStringLiteral("description"), QStringLiteral("tags"), QStringLiteral("categories"), QStringLiteral("duration"),
        QStringLiteral("channel"), QStringLiteral("channel_id"), QStringLiteral("channel_url"), QStringLiteral("extractor"), QStringLiteral("webpage_url"),
        QStringLiteral("playlist"), QStringLiteral("playlist_title"), QStringLiteral("playlist_id"), QStringLiteral("playlist_index"),
        QStringLiteral("artist"), QStringLiteral("track"), QStringLiteral("album_artist"), QStringLiteral("release_year"), QStringLiteral("release_date"),
        // Custom tokens for date parts
        QStringLiteral("upload_year"), QStringLiteral("upload_month"), QStringLiteral("upload_day")
    };

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString token = match.captured(1);
        if (!validTokens.contains(token.toLower())) {
            error = tr("The token '{%1}' is not a valid metadata field. Please check the spelling and try again.").arg(token);
            return false;
        }
    }
    return true;
}
