#pragma once

#include <QWidget>
#include <QString>
#include <QList>
#include <QFutureWatcher>

class QVBoxLayout;
class QScrollArea;

struct HistoryItemData {
    QString id;
    QString title;
    QString url;
    QString filePath;
    QString timestamp;
    QString thumbnailPath;
    qint64 totalBytes = 0;
    QString duration;
};

class DownloadHistoryTab : public QWidget {
    Q_OBJECT
public:
    explicit DownloadHistoryTab(QWidget *parent = nullptr);
    ~DownloadHistoryTab() override;

    void loadHistory(const QString &filePath);
    void saveHistory() const;

public slots:
    void addHistoryItem(const HistoryItemData &data);
    void clearHistory();

private:
    void saveHistoryAsync();
    void startHistorySave(const QString &path, const QList<HistoryItemData> &items);

    QScrollArea *m_scrollArea;
    QWidget *m_scrollWidget;
    QVBoxLayout *m_listLayout;

    QString m_historyFilePath;
    QList<HistoryItemData> m_historyItems;
    QFutureWatcher<void> *m_historySaveWatcher = nullptr;
    bool m_hasPendingHistorySave = false;
    QString m_pendingHistoryPath;
    QList<HistoryItemData> m_pendingHistoryItems;
};
