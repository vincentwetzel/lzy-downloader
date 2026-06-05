#pragma once

#include <QDialog>
#include <QVariantMap>
#include <QTableWidget>
#include <QStringList>

class QLabel;

class FormatSelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit FormatSelectionDialog(const QVariantMap &infoDict, const QVariantMap &options, QWidget *parent = nullptr);
    ~FormatSelectionDialog() override;

    QStringList getSelectedFormatIds() const;

private slots:
    void onSelectionChanged();

private:
    void setupUI();
    void populateTable(const QVariantMap &infoDict, const QString& downloadType);

    QTableWidget *m_table;
    QLabel *m_selectionSummary;
};

