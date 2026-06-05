#pragma once

#include <QWidget>
#include <QTableWidget> // Changed from QListWidget
#include <QPushButton>
#include "core/ConfigManager.h"
#include "core/SortingManager.h"

class SortingTab : public QWidget {
    Q_OBJECT

public:
    explicit SortingTab(ConfigManager *configManager, QWidget *parent = nullptr);

private slots:
    void addRule();
    void editRule();
    void deleteRule();
    void moveRuleUp();    // New slot
    void moveRuleDown();  // New slot

private:
    void setupUI();
    void loadRules();
    void saveRules(); // This might need to be adjusted or removed if rules are saved differently
    void updatePriorityNumbers(); // New helper function
    void populateRow(int row, const QVariantMap &rule); // New helper to populate a row from a rule map

    ConfigManager *m_configManager;
    SortingManager *m_sortingManager;

    QTableWidget *m_rulesTable; // Changed from QListWidget
    QPushButton *m_addButton;
    QPushButton *m_editButton;
    QPushButton *m_deleteButton;
    QPushButton *m_moveUpButton;   // New button
    QPushButton *m_moveDownButton; // New button
};

