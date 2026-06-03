#ifndef MAINWINDOWUIBUILDER_H
#define MAINWINDOWUIBUILDER_H

#include <QObject>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QMainWindow> // Include QMainWindow for the build method

class ConfigManager;
class StartTab;
class ActiveDownloadsTab;
class AdvancedSettingsTab;
class DownloadHistoryTab;
class SortingTab;
class ToggleSwitch;

class MainWindowUiBuilder : public QObject
{
    Q_OBJECT
public:
    explicit MainWindowUiBuilder(ConfigManager *configManager, QObject *parent = nullptr);
    void build(QMainWindow *mainWindow, QVBoxLayout *mainLayout, StartTab *startTab, ActiveDownloadsTab *activeDownloadsTab, DownloadHistoryTab *downloadHistoryTab, AdvancedSettingsTab *advancedSettingsTab, SortingTab *sortingTab);
    QLabel* speedLabel() const { return m_speedLabel; }
    QLabel* queuedDownloadsLabel() const { return m_queuedDownloadsLabel; }
    QLabel* activeDownloadsLabel() const { return m_activeDownloadsLabel; }
    QLabel* completedDownloadsLabel() const { return m_completedDownloadsLabel; }
    QLabel* errorDownloadsLabel() const { return m_errorDownloadsLabel; }
    ToggleSwitch* exitAfterSwitch() const { return m_exitAfterSwitch; }
    QTabWidget* tabWidget() const { return m_tabWidget; }
private:
    ConfigManager *m_configManager;
    QTabWidget *m_tabWidget;
    QLabel *m_speedLabel;
    QLabel *m_queuedDownloadsLabel;
    QLabel *m_activeDownloadsLabel;
    QLabel *m_completedDownloadsLabel;
    QLabel *m_errorDownloadsLabel;
    ToggleSwitch *m_exitAfterSwitch;
};

#endif // MAINWINDOWUIBUILDER_H