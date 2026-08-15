#pragma once

#include <QDialog>
#include <QStringList>

class ConfigManager;
class QCheckBox;
class QRadioButton;

class InitialBinarySetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit InitialBinarySetupDialog(ConfigManager *configManager,
                                      const QStringList &missingRequired,
                                      QWidget *parent = nullptr);

    [[nodiscard]] bool preferAppManagedBinaries() const;
    [[nodiscard]] QStringList binariesToInstall() const;

private:
    [[nodiscard]] bool hasSystemBinary() const;
    [[nodiscard]] bool binaryIsResolved(const QString &binaryName) const;
    void refreshSummary();

    ConfigManager *m_configManager;
    QRadioButton *m_systemFirstButton;
    QRadioButton *m_appManagedFirstButton;
    QCheckBox *m_galleryDlCheck;
    QCheckBox *m_aria2cCheck;
};
