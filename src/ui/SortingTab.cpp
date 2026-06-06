#include "SortingTab.h"
#include "SortingRuleDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QHeaderView>
#include <QDebug>
#include <QDir>
#include <QTableWidget>
#include <QPushButton>

SortingTab::SortingTab(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    setupUI();
    loadRules();
    updatePriorityNumbers();
}

void SortingTab::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    QLabel *descriptionLabel = new QLabel(tr("Organize your downloaded files automatically! Create rules to move files to specific folders based on their properties (like title, uploader, or file type)."), this);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setToolTip(tr("This section allows you to set up rules for automatically sorting your downloaded files into different folders."));
    mainLayout->addWidget(descriptionLabel);

    m_rulesTable = new QTableWidget(this);
    m_rulesTable->setToolTip(tr("This table shows all your active sorting rules. When a download finishes, the app checks these rules from top to bottom."));
    m_rulesTable->setColumnCount(6);
    m_rulesTable->setHorizontalHeaderLabels({tr("#"), tr("Name"), tr("Applies To"), tr("Condition"), tr("Target Path"), tr("Subfolder")});
    m_rulesTable->verticalHeader()->setVisible(false);
    m_rulesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_rulesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_rulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rulesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rulesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rulesTable->setSortingEnabled(false);
    mainLayout->addWidget(m_rulesTable);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_addButton = new QPushButton(tr("Add Rule"), this);
    m_addButton->setToolTip(tr("Click to create a new sorting rule."));
    m_editButton = new QPushButton(tr("Edit Rule"), this);
    m_editButton->setToolTip(tr("Select a rule from the list and click here to change it."));
    m_deleteButton = new QPushButton(tr("Delete Rule"), this);
    m_deleteButton->setToolTip(tr("Select a rule from the list and click here to delete it."));
    m_moveUpButton = new QPushButton(tr("Move Up"), this);
    m_moveUpButton->setToolTip(tr("Move the selected rule up in priority."));
    m_moveDownButton = new QPushButton(tr("Move Down"), this);
    m_moveDownButton->setToolTip(tr("Move the selected rule down in priority."));

    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_editButton);
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_moveUpButton);
    buttonLayout->addWidget(m_moveDownButton);

    mainLayout->addLayout(buttonLayout);

    connect(m_addButton, &QPushButton::clicked, this, &SortingTab::addRule);
    connect(m_editButton, &QPushButton::clicked, this, &SortingTab::editRule);
    connect(m_deleteButton, &QPushButton::clicked, this, &SortingTab::deleteRule);
    connect(m_moveUpButton, &QPushButton::clicked, this, &SortingTab::moveRuleUp);
    connect(m_moveDownButton, &QPushButton::clicked, this, &SortingTab::moveRuleDown);
    connect(m_rulesTable, &QTableWidget::itemDoubleClicked, this, &SortingTab::editRule);
}

void SortingTab::populateRow(int row, const QVariantMap &ruleMap) {
    QTableWidgetItem *priorityItem = new QTableWidgetItem(QString());
    // Store the map directly in the user role
    priorityItem->setData(Qt::UserRole, ruleMap);
    m_rulesTable->setItem(row, 0, priorityItem);

    m_rulesTable->setItem(row, 1, new QTableWidgetItem(ruleMap[QStringLiteral("name")].toString()));

    QString appliesToUI = ruleMap[QStringLiteral("applies_to")].toString();
    if (appliesToUI == QStringLiteral("video")) appliesToUI = tr("Video Downloads");
    else if (appliesToUI == QStringLiteral("audio")) appliesToUI = tr("Audio Downloads");
    else if (appliesToUI == QStringLiteral("gallery")) appliesToUI = tr("Gallery Downloads");
    else if (appliesToUI == QStringLiteral("video_playlist")) appliesToUI = tr("Video Playlist Downloads");
    else if (appliesToUI == QStringLiteral("audio_playlist")) appliesToUI = tr("Audio Playlist Downloads");
    else if (appliesToUI == QStringLiteral("any") || appliesToUI == QStringLiteral("all")) appliesToUI = tr("All Downloads");
    m_rulesTable->setItem(row, 2, new QTableWidgetItem(appliesToUI));

    QString conditionText = tr("No conditions");
    QVariantList conditions = ruleMap[QStringLiteral("conditions")].toList();
    if (!conditions.isEmpty()) {
        QVariantMap firstCondition = conditions.first().toMap();
        QString field = firstCondition[QStringLiteral("field")].toString();
        QString op = firstCondition[QStringLiteral("operator")].toString();
        QString value = firstCondition[QStringLiteral("value")].toString();

        if (op == QStringLiteral("Is One Of")) {
            int valueCount = value.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size();
            conditionText = tr("%1 is one of [%2 values]").arg(field).arg(valueCount);
        } else {
            conditionText = tr("%1 %2 \"%3\"").arg(field).arg(op.toLower()).arg(value);
        }

        if (conditions.size() > 1) {
            conditionText = tr("%1 (+%2 more)").arg(conditionText).arg(conditions.size() - 1);
        }
    }
    m_rulesTable->setItem(row, 3, new QTableWidgetItem(conditionText));
    m_rulesTable->setItem(row, 4, new QTableWidgetItem(QDir::toNativeSeparators(ruleMap[QStringLiteral("target_folder")].toString())));
    m_rulesTable->setItem(row, 5, new QTableWidgetItem(ruleMap[QStringLiteral("subfolder_pattern")].toString()));
}

void SortingTab::loadRules() {
    m_rulesTable->setRowCount(0);
    m_rulesTable->setUpdatesEnabled(false);
    int size = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("size"), 0).toInt();

    bool rulesPurged = false;

    for (int i = 0; i < size; ++i) {
        QString key = QStringLiteral("rule_%1").arg(i);
        QVariantMap ruleMap;
        
        ruleMap[QStringLiteral("name")] = m_configManager->get(QStringLiteral("SortingRules"), key + QStringLiteral("_name")).toString();
        
        QString rawAppliesTo = m_configManager->get(QStringLiteral("SortingRules"), key + QStringLiteral("_applies_to")).toString();
        QString appliesTo = rawAppliesTo;
        if (appliesTo.compare(QStringLiteral("Video Downloads"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("video");
        else if (appliesTo.compare(QStringLiteral("Audio Downloads"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("audio");
        else if (appliesTo.compare(QStringLiteral("Gallery Downloads"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("gallery");
        else if (appliesTo.compare(QStringLiteral("Video Playlist Downloads"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("video_playlist");
        else if (appliesTo.compare(QStringLiteral("Audio Playlist Downloads"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("audio_playlist");
        else if (appliesTo.compare(QStringLiteral("All Downloads"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("any");
        else if (appliesTo.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0) appliesTo = QStringLiteral("any");
        
        if (rawAppliesTo != appliesTo) {
            rulesPurged = true; // Force save to upgrade legacy config rules
        }
        ruleMap[QStringLiteral("applies_to")] = appliesTo;
        
        ruleMap[QStringLiteral("target_folder")] = m_configManager->get(QStringLiteral("SortingRules"), key + QStringLiteral("_target_folder")).toString();
        ruleMap[QStringLiteral("subfolder_pattern")] = m_configManager->get(QStringLiteral("SortingRules"), key + QStringLiteral("_subfolder_pattern")).toString();

        int condSize = m_configManager->get(QStringLiteral("SortingRules"), key + QStringLiteral("_conditions_size"), 0).toInt();
        QVariantList conditions;
        for (int j = 0; j < condSize; ++j) {
            QVariantMap cond;
            QString condKey = QStringLiteral("%1_condition_%2").arg(key).arg(j);
            cond[QStringLiteral("field")] = m_configManager->get(QStringLiteral("SortingRules"), condKey + QStringLiteral("_field")).toString();
            cond[QStringLiteral("operator")] = m_configManager->get(QStringLiteral("SortingRules"), condKey + QStringLiteral("_operator")).toString();
            cond[QStringLiteral("value")] = m_configManager->get(QStringLiteral("SortingRules"), condKey + QStringLiteral("_value")).toString();
            conditions.append(cond);
        }
        ruleMap[QStringLiteral("conditions")] = conditions;

        // Discard legacy JSON formats or invalid/empty rules
        if (ruleMap[QStringLiteral("name")].toString().isEmpty() || ruleMap[QStringLiteral("target_folder")].toString().isEmpty()) {
            qWarning() << "Skipping invalid or legacy sorting rule for key" << key;
            rulesPurged = true;
            continue;
        }

        int row = m_rulesTable->rowCount();
        m_rulesTable->insertRow(row);
        populateRow(row, ruleMap);
    }

    // Check for detached garbage past 'size'
    if (!m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("rule_%1").arg(size)).isNull() ||
        !m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("rule_%1_name").arg(size)).isNull()) {
        rulesPurged = true;
    }

    if (rulesPurged) {
        qInfo() << "Purging invalid/legacy rules from settings.";
        saveRules();
    }
    m_rulesTable->setUpdatesEnabled(true);
}

void SortingTab::saveRules() {
    int oldSize = m_configManager->get(QStringLiteral("SortingRules"), QStringLiteral("size"), 0).toInt();
    int newSize = m_rulesTable->rowCount();
    
    m_configManager->set(QStringLiteral("SortingRules"), QStringLiteral("size"), newSize);
    for (int i = 0; i < newSize; ++i) {
        QVariantMap ruleMap = m_rulesTable->item(i, 0)->data(Qt::UserRole).toMap();
        QString baseKey = QStringLiteral("rule_%1").arg(i);
        
        // Purge old JSON string key
        m_configManager->remove(QStringLiteral("SortingRules"), baseKey);
        
        // Save strictly in flat properties
        m_configManager->set(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_name"), ruleMap[QStringLiteral("name")]);
        m_configManager->set(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_applies_to"), ruleMap[QStringLiteral("applies_to")]);
        m_configManager->set(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_target_folder"), ruleMap[QStringLiteral("target_folder")]);
        m_configManager->set(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_subfolder_pattern"), ruleMap[QStringLiteral("subfolder_pattern")]);
        
        QVariantList conditions = ruleMap[QStringLiteral("conditions")].toList();
        int oldCondSize = m_configManager->get(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_conditions_size"), 0).toInt();
        m_configManager->set(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_conditions_size"), conditions.size());
        
        for (int j = 0; j < conditions.size(); ++j) {
            QVariantMap cond = conditions[j].toMap();
            QString condKey = QStringLiteral("%1_condition_%2").arg(baseKey).arg(j);
            m_configManager->set(QStringLiteral("SortingRules"), condKey + QStringLiteral("_field"), cond[QStringLiteral("field")]);
            m_configManager->set(QStringLiteral("SortingRules"), condKey + QStringLiteral("_operator"), cond[QStringLiteral("operator")]);
            m_configManager->set(QStringLiteral("SortingRules"), condKey + QStringLiteral("_value"), cond[QStringLiteral("value")]);
        }
        
        // Purge leftover conditions if the rule shrunk
        for (int j = conditions.size(); j < oldCondSize; ++j) {
            QString condKey = QStringLiteral("%1_condition_%2").arg(baseKey).arg(j);
            m_configManager->remove(QStringLiteral("SortingRules"), condKey + QStringLiteral("_field"));
            m_configManager->remove(QStringLiteral("SortingRules"), condKey + QStringLiteral("_operator"));
            m_configManager->remove(QStringLiteral("SortingRules"), condKey + QStringLiteral("_value"));
        }
    }
    
    // Purge leftover rules entirely, checking up to a large bound to catch detached legacy keys
    int cleanupLimit = qMax(oldSize, 100);
    for (int i = newSize; i < cleanupLimit; ++i) {
        QString baseKey = QStringLiteral("rule_%1").arg(i);
        
        m_configManager->remove(QStringLiteral("SortingRules"), baseKey);
        m_configManager->remove(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_name"));
        m_configManager->remove(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_applies_to"));
        m_configManager->remove(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_target_folder"));
        m_configManager->remove(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_subfolder_pattern"));
        
        int oldCondSize = m_configManager->get(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_conditions_size"), 0).toInt();
        m_configManager->remove(QStringLiteral("SortingRules"), baseKey + QStringLiteral("_conditions_size"));
        for (int j = 0; j < qMax(oldCondSize, 20); ++j) {
            QString condKey = QStringLiteral("%1_condition_%2").arg(baseKey).arg(j);
            m_configManager->remove(QStringLiteral("SortingRules"), condKey + QStringLiteral("_field"));
            m_configManager->remove(QStringLiteral("SortingRules"), condKey + QStringLiteral("_operator"));
            m_configManager->remove(QStringLiteral("SortingRules"), condKey + QStringLiteral("_value"));
        }
    }
    
    m_configManager->save();
}

void SortingTab::addRule() {
    SortingRuleDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QVariantMap rule = dialog.getRule();
        int row = m_rulesTable->rowCount();
        m_rulesTable->insertRow(row);
        populateRow(row, rule);
        updatePriorityNumbers();
        m_rulesTable->setCurrentCell(row, 0); // Select the newly added rule
        saveRules();
    }
}

void SortingTab::editRule() {
    int currentRow = m_rulesTable->currentRow();
    if (currentRow >= 0) {
        QTableWidgetItem *item = m_rulesTable->item(currentRow, 0);
        if (!item) {
            return;
        }
        QVariantMap originalRule = item->data(Qt::UserRole).toMap();
        SortingRuleDialog dialog(originalRule, this);

        if (dialog.exec() == QDialog::Accepted) {
            QVariantMap newRule = dialog.getRule();
            // Only update and save if the rule has actually changed.
            if (originalRule != newRule) {
                populateRow(currentRow, newRule);
                updatePriorityNumbers();
                m_rulesTable->setCurrentCell(currentRow, 0); // Restore selection after item recreation
                saveRules();
                qDebug() << "Sorting rule changed, saving to disk.";
            } else {
                qDebug() << "Sorting rule unchanged, skipping save.";
            }
        }
    } else {
        QMessageBox::warning(this, tr("Edit Rule"), tr("Please select a rule to edit."));
    }
}

void SortingTab::deleteRule() {
    int currentRow = m_rulesTable->currentRow();
    if (currentRow >= 0) {
        if (QMessageBox::question(this, tr("Delete Rule"), tr("Are you sure you want to remove this rule?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            m_rulesTable->removeRow(currentRow);
            updatePriorityNumbers();
            saveRules();
        }
    } else {
        QMessageBox::warning(this, tr("Delete Rule"), tr("Please select a rule to remove."));
    }
}

void SortingTab::moveRuleUp() {
    int currentRow = m_rulesTable->currentRow();
    if (currentRow > 0) {
        QTableWidgetItem *currentItem = m_rulesTable->item(currentRow, 0);
        QTableWidgetItem *aboveItem = m_rulesTable->item(currentRow - 1, 0);
        if (!currentItem || !aboveItem) return;

        QVariantMap currentRule = currentItem->data(Qt::UserRole).toMap();
        QVariantMap aboveRule = aboveItem->data(Qt::UserRole).toMap();

        populateRow(currentRow, aboveRule);
        populateRow(currentRow - 1, currentRule);

        updatePriorityNumbers();
        m_rulesTable->setCurrentCell(currentRow - 1, 0);
        saveRules();
    } else if (currentRow == -1) {
        QMessageBox::warning(this, tr("Move Rule"), tr("Please select a rule to move."));
    }
}

void SortingTab::moveRuleDown() {
    int currentRow = m_rulesTable->currentRow();
    if (currentRow >= 0 && currentRow < m_rulesTable->rowCount() - 1) {
        QTableWidgetItem *currentItem = m_rulesTable->item(currentRow, 0);
        QTableWidgetItem *belowItem = m_rulesTable->item(currentRow + 1, 0);
        if (!currentItem || !belowItem) return;

        QVariantMap currentRule = currentItem->data(Qt::UserRole).toMap();
        QVariantMap belowRule = belowItem->data(Qt::UserRole).toMap();

        populateRow(currentRow, belowRule);
        populateRow(currentRow + 1, currentRule);

        updatePriorityNumbers();
        m_rulesTable->setCurrentCell(currentRow + 1, 0);
        saveRules();
    } else if (currentRow == -1) {
        QMessageBox::warning(this, tr("Move Rule"), tr("Please select a rule to move."));
    }
}

void SortingTab::updatePriorityNumbers() {
    for (int i = 0; i < m_rulesTable->rowCount(); ++i) {
        QTableWidgetItem *item = m_rulesTable->item(i, 0);
        if (item) {
            item->setText(QString::number(i + 1));
        } else {
            m_rulesTable->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        }
    }
}
