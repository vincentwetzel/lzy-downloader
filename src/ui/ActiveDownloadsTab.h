#ifndef ACTIVEDOWNLOADSTAB_H
#define ACTIVEDOWNLOADSTAB_H

#include <QWidget>
#include <QMap>
#include <QVariantMap>

class QVBoxLayout;
class QStackedLayout;
class DownloadItemWidget;
class ConfigManager;
class QPushButton;
class QLabel;

class ActiveDownloadsTab : public QWidget {
    Q_OBJECT

public:
    explicit ActiveDownloadsTab(ConfigManager *configManager, QWidget *parent = nullptr);

public slots:
    void addDownloadItem(const QVariantMap &itemData);
    void updateDownloadProgress(const QString &id, const QVariantMap &progressData);
    void onDownloadFinished(const QString &id, bool success, const QString &message);
    void onDownloadCancelled(const QString &id);
    void onDownloadFinalPathReady(const QString &id, const QString &path);
    void setDownloadStatus(const QString &id, const QString &status);
    void addExpandingPlaylist(const QString &url);
    void removeExpandingPlaylist(const QString &url, int count);
    void onDownloadPaused(const QString &id);
    void onDownloadResumed(const QString &id);
    void removeDownloadItem(const QString &id);

signals:
    void cancelDownloadRequested(const QString &id);
    void retryDownloadRequested(const QVariantMap &itemData);
    void resumeDownloadRequested(const QVariantMap &itemData);
    void pauseDownloadRequested(const QString &id);
    void unpauseDownloadRequested(const QString &id);
    void moveDownloadUpRequested(const QString &id);
    void moveDownloadDownRequested(const QString &id);
    void finishDownloadRequested(const QString &id);
    void itemCleared(const QString &id, bool wasSuccessful, bool wasFinished);

private:
    void setupUi();
    void updatePlaceholderVisibility();
    void cancelAllDownloads();
    void togglePauseAllDownloads();
    void onItemClearRequested(const QString &id);
    void onItemMoveUpRequested(const QString &id);
    void onItemMoveDownRequested(const QString &id);

    ConfigManager *m_configManager;
    QStackedLayout *m_stackedLayout;
    QWidget *m_downloadsContainer;
    QVBoxLayout *m_downloadsLayout;
    QWidget *m_placeholderWidget;
    QPushButton *m_pauseResumeAllButton;
    QPushButton *m_cancelAllButton;
    QPushButton *m_clearInactiveButton;
    bool m_isAllPaused = false;

    QMap<QString, DownloadItemWidget*> m_downloadItems;
    QMap<QString, QWidget*> m_expandingPlaylists;
};

#endif // ACTIVEDOWNLOADSTAB_H
