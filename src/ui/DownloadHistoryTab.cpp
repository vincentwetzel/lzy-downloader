#include "DownloadHistoryTab.h"
#include "DownloadHistoryStorage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QPixmap>
#include <QImageReader>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QPushButton>
#include <QDateTime>
#include <QPalette>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

// A widget to represent a single history item
class DownloadHistoryItemWidget : public QFrame {
public:
    explicit DownloadHistoryItemWidget(const HistoryItemData &data, QWidget *parent = nullptr)
        : QFrame(parent) {
        setProperty("historyId", data.id);
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Raised);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        
        QHBoxLayout *mainLayout = new QHBoxLayout(this);
        
        // Thumbnail
        QLabel *thumbnailLabel = new QLabel(this);
        thumbnailLabel->setObjectName(QStringLiteral("thumbnailLabel"));
        thumbnailLabel->setProperty("thumbnailPath", data.thumbnailPath);
        thumbnailLabel->setFixedSize(120, 68);
        thumbnailLabel->setAlignment(Qt::AlignCenter);
        
        if (!data.thumbnailPath.isEmpty()) {
            if (data.thumbnailPath.startsWith(QStringLiteral("http://")) || data.thumbnailPath.startsWith(QStringLiteral("https://"))) {
                QNetworkAccessManager *manager = qApp->findChild<QNetworkAccessManager*>(QStringLiteral("sharedThumbnailManager"));
                if (!manager) {
                    manager = new QNetworkAccessManager(qApp);
                    manager->setObjectName(QStringLiteral("sharedThumbnailManager"));
                }
                QNetworkRequest request(QUrl(data.thumbnailPath));
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
                request.setTransferTimeout(15000);
                QNetworkReply *reply = manager->get(request);
                connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
                connect(reply, &QNetworkReply::finished, this, [thumbnailLabel, reply]() {
                    if (reply->error() == QNetworkReply::NoError) {
                        QPixmap pixmap;
                        if (pixmap.loadFromData(reply->readAll())) {
                            thumbnailLabel->setPixmap(pixmap.scaled(120, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                        } else {
                            thumbnailLabel->setText(tr("No Image"));
                        }
                    } else {
                        thumbnailLabel->setText(tr("No Image"));
                    }
                });
            } else if (!data.thumbnailPath.isEmpty()) {
                loadThumbnailAsync(data.thumbnailPath, thumbnailLabel);
            } else {
                thumbnailLabel->setText(tr("No Image"));
            }
        } else {
            thumbnailLabel->setText(tr("No Image"));
        }
        
        mainLayout->addWidget(thumbnailLabel);
        
        // Info Layout
        QVBoxLayout *infoLayout = new QVBoxLayout();
        
        QLabel *titleLabel = new QLabel(data.title.isEmpty() ? tr("Unknown Title") : data.title, this);
        titleLabel->setTextFormat(Qt::PlainText);
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleLabel->setFont(titleFont);
        titleLabel->setWordWrap(true);
        
        QLabel *urlLabel = new QLabel(this);
        const QUrl sourceUrl(data.url);
        const bool canOpenSourceUrl = sourceUrl.isValid()
            && !sourceUrl.scheme().isEmpty()
            && !sourceUrl.host().isEmpty()
            && (sourceUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
                || sourceUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0);
        if (canOpenSourceUrl) {
            const QString escapedUrl = data.url.toHtmlEscaped();
            urlLabel->setText(QStringLiteral("<a href=\"%1\">%1</a>").arg(escapedUrl));
            urlLabel->setTextFormat(Qt::RichText);
            urlLabel->setOpenExternalLinks(true);
            urlLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
            urlLabel->setCursor(Qt::PointingHandCursor);
            urlLabel->setToolTip(tr("Open the original source URL"));
        } else {
            urlLabel->setText(data.url);
            urlLabel->setTextFormat(Qt::PlainText);
            urlLabel->setToolTip(tr("Original source URL is unavailable"));
        }
        QFont urlFont = urlLabel->font();
        urlFont.setPointSize(qMax(8, urlFont.pointSize() - 1));
        urlLabel->setFont(urlFont);
        
        QString sizeStr = data.totalBytes > 0 ? tr("%1 MB").arg(QString::number(data.totalBytes / (1024.0 * 1024.0), 'f', 2)) : tr("Unknown Size");
        
        QString detailsText;
        if (!data.duration.isEmpty()) {
            detailsText = tr("%1 • %2 • %3").arg(data.timestamp, sizeStr, data.duration);
        } else {
            detailsText = tr("%1 • %2").arg(data.timestamp, sizeStr);
        }
        
        QLabel *detailsLabel = new QLabel(detailsText, this);

        infoLayout->addWidget(titleLabel);
        infoLayout->addWidget(urlLabel);
        infoLayout->addWidget(detailsLabel);
        infoLayout->addStretch();
        
        mainLayout->addLayout(infoLayout);
        
        // Action Buttons
        QVBoxLayout *actionLayout = new QVBoxLayout();
        
        QPushButton *openFileBtn = new QPushButton(tr("Open File"), this);
        openFileBtn->setToolTip(tr("Open the downloaded file"));
        connect(openFileBtn, &QPushButton::clicked, this, [data, this]() {
            if (QFileInfo::exists(data.filePath)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(data.filePath));
            }
        });
        
        QPushButton *openFolderBtn = new QPushButton(tr("Open Folder"), this);
        openFolderBtn->setToolTip(tr("Open the folder containing the file"));
        connect(openFolderBtn, &QPushButton::clicked, this, [data, this]() {
            QFileInfo fi(data.filePath);
            if (fi.exists()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
            }
        });
        
        actionLayout->addWidget(openFileBtn);
        actionLayout->addWidget(openFolderBtn);
        actionLayout->addStretch();
        
        mainLayout->addLayout(actionLayout);
    }

    ~DownloadHistoryItemWidget() override
    {
        // QThread::create() workers are owned by this row. Wait for any
        // outstanding file read before QObject tears down the label and its
        // queued callbacks. This is normally only a few milliseconds, while
        // preventing a Windows heap corruption during rapid row teardown.
        while (!m_thumbnailThreads.isEmpty()) {
            QThread *thread = m_thumbnailThreads.takeLast();
            if (thread->isRunning()) {
                thread->requestInterruption();
                thread->wait();
            }
        }
    }

    void updateThumbnail(const QString &thumbnailPath)
    {
        if (thumbnailPath.isEmpty()) {
            return;
        }

        QLabel *thumbnailLabel = findChild<QLabel *>(QStringLiteral("thumbnailLabel"));
        if (!thumbnailLabel) {
            return;
        }

        thumbnailLabel->setProperty("thumbnailPath", thumbnailPath);
        loadThumbnailAsync(thumbnailPath, thumbnailLabel);
    }

private:
    void loadThumbnailAsync(const QString &thumbnailPath, QLabel *thumbnailLabel)
    {
        QPointer<QLabel> label(thumbnailLabel);
        QPointer<QCoreApplication> application(QCoreApplication::instance());
        QThread *thread = QThread::create([thumbnailPath, label, application]() {
            QImageReader reader(thumbnailPath);
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (!application || !label) {
                return;
            }
            QMetaObject::invokeMethod(application.data(), [label, image, thumbnailPath]() {
                if (!label || label->property("thumbnailPath").toString() != thumbnailPath) {
                    return;
                }
                if (!image.isNull()) {
                    label->setPixmap(QPixmap::fromImage(image).scaled(120, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                } else {
                    label->setText(QObject::tr("No Image"));
                }
            }, Qt::QueuedConnection);
        });
        thread->setParent(this);
        m_thumbnailThreads.append(thread);
        QObject::connect(thread, &QThread::finished, this, [this, thread]() {
            m_thumbnailThreads.removeOne(thread);
        });
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }

    QList<QThread *> m_thumbnailThreads;
};

DownloadHistoryTab::DownloadHistoryTab(QWidget *parent) : QWidget(parent) {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    // Controls Layout
    QHBoxLayout *controlsLayout = new QHBoxLayout();
    QLabel *titleLabel = new QLabel(tr("<b>Download History</b>"), this);
    QPushButton *clearBtn = new QPushButton(tr("Clear History"), this);
    clearBtn->setToolTip(tr("Clear all history entries (does not delete downloaded files)"));
    
    connect(clearBtn, &QPushButton::clicked, this, &DownloadHistoryTab::clearHistory);
    
    controlsLayout->addWidget(titleLabel);
    controlsLayout->addStretch();
    controlsLayout->addWidget(clearBtn);
    
    mainLayout->addLayout(controlsLayout);
    
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    
    m_scrollWidget = new QWidget(m_scrollArea);
    m_listLayout = new QVBoxLayout(m_scrollWidget);
    m_listLayout->setAlignment(Qt::AlignTop);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch();
    
    m_scrollArea->setWidget(m_scrollWidget);
    mainLayout->addWidget(m_scrollArea);

    m_historySaveWatcher = new QFutureWatcher<void>(this);
    connect(m_historySaveWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        if (!m_hasPendingHistorySave) {
            return;
        }
        const QString path = m_pendingHistoryPath;
        const QList<HistoryItemData> items = std::move(m_pendingHistoryItems);
        m_pendingHistoryPath.clear();
        m_hasPendingHistorySave = false;
        startHistorySave(path, items);
    });
}

DownloadHistoryTab::~DownloadHistoryTab()
{
    if (m_historySaveWatcher && m_historySaveWatcher->isRunning()) {
        m_historySaveWatcher->waitForFinished();
    }
    if (m_hasPendingHistorySave) {
        DownloadHistoryStorage::saveHistoryToPath(m_pendingHistoryPath, m_pendingHistoryItems);
    }
}

void DownloadHistoryTab::loadHistory(const QString &filePath) {
    m_historyFilePath = filePath;
    m_historyItems = DownloadHistoryStorage::loadHistoryFromPath(filePath);
    rebuildHistoryRows();
}

void DownloadHistoryTab::loadHistoryAsync(const QString &filePath) {
    m_historyFilePath = filePath;
    auto *watcher = new QFutureWatcher<QList<HistoryItemData>>(this);
    connect(watcher, &QFutureWatcher<QList<HistoryItemData>>::finished, this, [this, watcher]() {
        m_historyItems = watcher->result();
        watcher->deleteLater();
        rebuildHistoryRows();
    });
    watcher->setFuture(QtConcurrent::run(DownloadHistoryStorage::loadHistoryFromPath, filePath));
}

void DownloadHistoryTab::setRenderingEnabled(bool enabled)
{
    if (m_renderingEnabled == enabled) {
        return;
    }
    m_renderingEnabled = enabled;
    ++m_historyRenderGeneration;
    if (enabled) {
        rebuildHistoryRows();
    } else {
        m_historyRenderItems.clear();
        m_historyRenderIndex = -1;
        QLayoutItem *item;
        while ((item = m_listLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                delete item->widget();
            }
            delete item;
        }
        m_listLayout->addStretch();
    }
}

void DownloadHistoryTab::rebuildHistoryRows()
{
    ++m_historyRenderGeneration;
    if (!m_renderingEnabled) {
        return;
    }

    QLayoutItem *item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_listLayout->addStretch();
    
    m_historyRenderItems = m_historyItems;
    m_historyRenderIndex = m_historyRenderItems.size() - 1;
    appendHistoryRows(m_historyRenderGeneration);
}

void DownloadHistoryTab::appendHistoryRows(int generation)
{
    if (!m_renderingEnabled || generation != m_historyRenderGeneration) {
        return;
    }

    m_scrollWidget->setUpdatesEnabled(false);
    for (int count = 0; count < 8 && m_historyRenderIndex >= 0; ++count, --m_historyRenderIndex) {
        auto *itemWidget = new DownloadHistoryItemWidget(m_historyRenderItems[m_historyRenderIndex], m_scrollWidget);
        m_listLayout->insertWidget(0, itemWidget);
    }
    m_scrollWidget->setUpdatesEnabled(true);

    if (m_historyRenderIndex >= 0) {
        QTimer::singleShot(1, this, [this, generation]() { appendHistoryRows(generation); });
    } else {
        m_historyRenderItems.clear();
    }
}

void DownloadHistoryTab::saveHistory() const {
    DownloadHistoryStorage::saveHistoryToPath(m_historyFilePath, m_historyItems);
}

void DownloadHistoryTab::updateThumbnail(const QString &id, const QString &thumbnailPath)
{
    const auto updateSnapshot = [&id, &thumbnailPath](QList<HistoryItemData> &items) {
        for (HistoryItemData &item : items) {
            if (item.id == id) {
                item.thumbnailPath = thumbnailPath;
            }
        }
    };
    updateSnapshot(m_historyItems);
    updateSnapshot(m_historyRenderItems);
    saveHistoryAsync();

    for (QFrame *frame : m_scrollWidget->findChildren<QFrame *>()) {
        if (frame->property("historyId").toString() == id) {
            static_cast<DownloadHistoryItemWidget *>(frame)->updateThumbnail(thumbnailPath);
            break;
        }
    }
}

void DownloadHistoryTab::saveHistoryAsync()
{
    if (m_historyFilePath.isEmpty() || !m_historySaveWatcher) {
        return;
    }

    if (m_historySaveWatcher->isRunning()) {
        m_pendingHistoryPath = m_historyFilePath;
        m_pendingHistoryItems = m_historyItems;
        m_hasPendingHistorySave = true;
        return;
    }

    startHistorySave(m_historyFilePath, m_historyItems);
}

void DownloadHistoryTab::startHistorySave(const QString &path, const QList<HistoryItemData> &items)
{
    m_historySaveWatcher->setFuture(QtConcurrent::run([path, items]() {
        DownloadHistoryStorage::saveHistoryToPath(path, items);
    }));
}

void DownloadHistoryTab::addHistoryItem(const HistoryItemData &data) {
    m_historyItems.insert(0, data);
    if (m_historyItems.size() > 500) {
        m_historyItems.removeLast();
    }
    saveHistoryAsync();

    if (m_renderingEnabled) {
        DownloadHistoryItemWidget *itemWidget = new DownloadHistoryItemWidget(data, m_scrollWidget);
        // Insert at index 0 so newest items appear at the top
        m_listLayout->insertWidget(0, itemWidget);
    }

    while (m_renderingEnabled && m_listLayout->count() > 501) { // 500 max widgets + 1 stretch
        QLayoutItem *item = m_listLayout->takeAt(m_listLayout->count() - 2);
        if (item && item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

void DownloadHistoryTab::clearHistory() {
    ++m_historyRenderGeneration;
    m_historyRenderItems.clear();
    m_historyRenderIndex = -1;
    m_historyItems.clear();
    saveHistoryAsync();
    
    m_scrollWidget->setUpdatesEnabled(false);
    QLayoutItem *item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_listLayout->addStretch();
    m_scrollWidget->setUpdatesEnabled(true);
}
