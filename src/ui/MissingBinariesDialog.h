#pragma once

#include <QDialog>
#include <QHash>
#include <QStringList>

class BinariesPage;
class ConfigManager;
class QLabel;
class QPushButton;

class MissingBinariesDialog : public QDialog {
    Q_OBJECT

public:
    explicit MissingBinariesDialog(const QStringList &binaryNames,
                                   ConfigManager *configManager,
                                   BinariesPage *binariesPage,
                                   const QHash<QString, QString> &updateDetails = {},
                                   QWidget *parent = nullptr);

    bool allBinariesResolved() const;

private:
    struct BinaryRow {
        QLabel *statusLabel = nullptr;
        QPushButton *actionButton = nullptr;
        QPushButton *browseButton = nullptr;
    };

    void refreshStatuses();
    void runUpdateAll();
    [[nodiscard]] bool needsAttention(const QString &binaryName) const;
    QStringList unresolvedBinaries() const;
    static QStringList normalizedBinaryList(const QStringList &binaryNames);

    QStringList m_binaryNames;
    ConfigManager *m_configManager;
    BinariesPage *m_binariesPage;
    QHash<QString, QString> m_updateDetails;
    QHash<QString, BinaryRow> m_rows;
    QLabel *m_summaryLabel;
    QPushButton *m_updateAllButton;
    QPushButton *m_laterButton;
};
