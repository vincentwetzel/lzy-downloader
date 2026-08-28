#pragma once

#include <QMap>
#include <QSet>
#include <QByteArray>
#include <QWidget>
#include <QVariant>

class ConfigManager;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QEvent;
class QScrollArea;
class BaseBinaryUpdater;

class BinariesPage : public QWidget {
    Q_OBJECT

public:
    explicit BinariesPage(ConfigManager *configManager, QWidget *parent = nullptr);
    void browseBinaryFor(const QString &binaryName);
    void installBinaryFor(const QString &binaryName);
    void installRecommendedBinary(const QString &binaryName, bool closeWhenFinished = false);
    void updateBinaryFor(const QString &binaryName, bool askForConfirmation = true,
                         bool closeWhenFinished = false);
    [[nodiscard]] QString recommendedInstallLabel(const QString &binaryName) const;
    [[nodiscard]] bool canUpdateBinaryAutomatically(const QString &binaryName) const;
    bool tryAutomaticUpdate(const QString &binaryName);
    void setBinaryWarning(const QString &binaryName, const QString &details);
    void refreshBinaryStatus(const QString &binaryName);
    void setGalleryDlVersion(const QString &version);
    void setYtDlpVersion(const QString &version);

public slots:
    void loadSettings();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value);

private:
    struct InstallOption {
        QString label;
        QString description;
        QString program;
        QStringList arguments;
        QVariantMap extraData;
    };

    struct ProcessRunOptions {
        QString dialogTitle;
        QString program;
        QStringList arguments;
        QString binaryName;
        bool isAlias = false;
        QString setCustomPath;
        QVariantMap environmentOverrides;
        QString followUpProgram;
        QStringList followUpArguments;
        QString cleanupPath;
        bool isUpdate = false;
        bool closeWhenFinished = false;
    };

    void runProcessWithLog(const ProcessRunOptions &opts);
    void resizeScrollDocument(QScrollArea *scrollArea);
    void scheduleScrollDocumentResize();

    void setupRow(QVBoxLayout *layout,
                  const QString &binaryName,
                  const QString &labelText,
                  const QString &configKey,
                  const QString &manualUrl,
                  bool optional = false);

    void fetchBinaryVersion(const QString &binaryName, const QString &path);
    QString browseBinary(const QString &title) const;
    void saveBinaryOverride(const QString &binaryName, const QString &path);
    QString resolvedPathForBinary(const QString &binaryName) const;
    QList<InstallOption> buildInstallOptions(const QString &binaryName) const;
    QString commandPreview(const InstallOption &option) const;
    QString displayName(const QString &binaryName) const;
    bool automaticUpdateIsDue(const QString &binaryName) const;
    bool isAppManagedPath(const QString &path) const;

    ConfigManager *m_configManager;
    QMap<QString, QString> m_configKeys;
    QMap<QString, QString> m_manualUrls;
    QMap<QString, QString> m_displayNames;
    QMap<QString, QString> m_binaryWarnings;
    QSet<QString> m_optionalBinaries;
    QMap<QString, QLabel *> m_statusLabels;
    QMap<QString, QLabel *> m_versionLabels;
    QMap<QString, QPushButton *> m_installButtons;
    QMap<QString, QPushButton *> m_updateButtons;
    QMap<QString, BaseBinaryUpdater *> m_updaters;
    bool m_scrollResizeQueued = false;
};
