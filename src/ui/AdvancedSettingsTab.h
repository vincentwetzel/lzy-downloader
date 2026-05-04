#ifndef ADVANCEDSETTINGSTAB_H
#define ADVANCEDSETTINGSTAB_H

#include "core/ConfigManager.h"
#include <QtWidgets/QWidget>

class QEvent;

class QListWidget;
class QStackedWidget;
class QPushButton;

class AdvancedSettingsTab : public QWidget {
    Q_OBJECT

public:
    explicit AdvancedSettingsTab(ConfigManager *configManager, QWidget *parent = nullptr);
    ~AdvancedSettingsTab();

signals:
    void themeChanged(const QString &themeName);

public slots:
    void setGalleryDlVersion(const QString &version);
    void setYtDlpVersion(const QString &version);
    void navigateToCategory(const QString &categoryTitle);

private slots:
    void restoreDefaults();
    void onThemeChanged(const QString &themeName);

private:
    void setupUI();
    void loadSettings();
    void applyCategoryListStyleSheet();


    ConfigManager *m_configManager;

    QListWidget *m_categoryList;
    QStackedWidget *m_stackedWidget;

    // Restore Defaults
    QPushButton *m_restoreDefaultsButton;

protected:
    void changeEvent(QEvent *event) override;
};

#endif // ADVANCEDSETTINGSTAB_H
