#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QScrollArea>
#include <QVariantMap>
#include <QPushButton>
#include <QVBoxLayout>

class SortingRuleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SortingRuleDialog(QWidget *parent = nullptr);
    explicit SortingRuleDialog(const QVariantMap &rule, QWidget *parent = nullptr);

    QVariantMap getRule() const;

private slots:
    void browseTargetFolder();
    void addCondition(const QVariantMap &condition = {});
    void insertToken(const QString &token);
    void accept() override;

private:
    void setupUI();
    void setRule(const QVariantMap &rule);
    bool validateSubfolderPattern(const QString &pattern, QString &error) const;

    QLineEdit *m_ruleNameInput;
    QLineEdit *m_targetFolderInput;
    QPushButton *m_browseButton;
    QLineEdit *m_subfolderPatternInput;
    QComboBox *m_tokenDropdown;
    QComboBox *m_appliesToDropdown;
    QScrollArea *m_conditionsScrollArea;
    QWidget *m_conditionsContainer;
    QVBoxLayout *m_conditionsLayout;
    QPushButton *m_addConditionButton;
};

