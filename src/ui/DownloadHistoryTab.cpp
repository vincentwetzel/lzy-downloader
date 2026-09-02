#include "DownloadHistoryTab.h"

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
#include <QFile>
#include <QSaveFile>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

namespace {
void saveHistoryToPath(const QString &path, const QList<HistoryItemData> &items)
{
    if (path.isEmpty()) {
        return;
    }

    QJsonArray arr;
    for (const HistoryItemData &data : items) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = data.id;
        obj[QStringLiteral("title")] = data.title;
        obj[QStringLiteral("url")] = data.url;
        obj[QStringLiteral("filePath")] = data.filePath;
        obj[QStringLiteral("timestamp")] = data.timestamp;
        obj[QStringLiteral("thumbnailPath")] = data.thumbnailPath;
        obj[QStringLiteral("totalBytes")] = data.totalBytes;
        obj[QStringLiteral("duration")] = data.duration;
        arr.append(obj);
    }

    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson());
        if (!file.commit()) {
            qWarning() << "Failed to commit download history to" << path;
        }
    }
}
}

// A widget to represent a single history item
class DownloadHistoryItemWidget : public QFrame {
public:
    explicit DownloadHistoryItemWidget(const HistoryItemData &data, QWidget *parent = nullptr)
        : QFrame(parent) {
        setFrameShape(QFrame::StyledPanel);
        setFrameShadow(QFrame::Raised);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        
        QHBoxLayout *mainLayout = new QHBoxLayout(this);
        
        // Thumbnail
        QLabel *thumbnailLabel = new QLabel(this);
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
                const QString thumbnailPath = data.thumbnailPath;
                QPointer<QLabel> label(thumbnailLabel);
                QCoreApplication *application = QCoreApplication::instance();
                QThread *thread = QThread::create([thumbnailPath, label, application]() {
                    QImageReader reader(thumbnailPath);
                    reader.setAutoTransform(true);
                    const QImage image = reader.read();
                    if (!application) {
                        return;
                    }
                    QMetaObject::invokeMethod(application, [label, image]() {
                        if (!label) {
                            return;
                        }
                        if (!image.isNull()) {
                            label->setPixmap(QPixmap::fromImage(image).scaled(120, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                        } else {
                            label->setText(QObject::tr("No Image"));
                        }
                    }, Qt::QueuedConnection);
                });
                QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
                thread->start();
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
        saveHistoryToPath(m_pendingHistoryPath, m_pendingHistoryItems);
    }
}

void DownloadHistoryTab::loadHistory(const QString &filePath) {
    m_historyFilePath = filePath;
    m_historyItems.clear();
    
    QFile file(m_historyFilePath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "Failed to parse download history JSON from" << m_historyFilePath << "Error:" << parseError.errorString();
            file.close();
            return;
        }

        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue &val : arr) {
                if (!val.isObject()) {
                    continue;
                }
                QJsonObject obj = val.toObject();
                HistoryItemData data;
                if (obj.contains(QStringLiteral("id")) && obj[QStringLiteral("id")].isString()) {
                    data.id = obj[QStringLiteral("id")].toString();
                }
                if (obj.contains(QStringLiteral("title")) && obj[QStringLiteral("title")].isString()) {
                    data.title = obj[QStringLiteral("title")].toString();
                }
                if (obj.contains(QStringLiteral("url")) && obj[QStringLiteral("url")].isString()) {
                    data.url = obj[QStringLiteral("url")].toString();
                }
                if (obj.contains(QStringLiteral("filePath")) && obj[QStringLiteral("filePath")].isString()) {
                    data.filePath = obj[QStringLiteral("filePath")].toString();
                }
                if (obj.contains(QStringLiteral("timestamp")) && obj[QStringLiteral("timestamp")].isString()) {
                    data.timestamp = obj[QStringLiteral("timestamp")].toString();
                }
                if (obj.contains(QStringLiteral("thumbnailPath")) && obj[QStringLiteral("thumbnailPath")].isString()) {
                    data.thumbnailPath = obj[QStringLiteral("thumbnailPath")].toString();
                }
                if (obj.contains(QStringLiteral("totalBytes")) && (obj[QStringLiteral("totalBytes")].isDouble() || obj[QStringLiteral("totalBytes")].isNull())) {
                    data.totalBytes = obj[QStringLiteral("totalBytes")].toVariant().toLongLong();
                }
                if (obj.contains(QStringLiteral("duration")) && obj[QStringLiteral("duration")].isString()) {
                    data.duration = obj[QStringLiteral("duration")].toString();
                }
                m_historyItems.append(data);
            }
        }
        file.close();
    }
    
    QLayoutItem *item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_listLayout->addStretch();
    
    m_scrollWidget->setUpdatesEnabled(false);

    for (int i = m_historyItems.size() - 1; i >= 0; --i) {
        DownloadHistoryItemWidget *itemWidget = new DownloadHistoryItemWidget(m_historyItems[i], m_scrollWidget);
        m_listLayout->insertWidget(0, itemWidget);
    }

    m_scrollWidget->setUpdatesEnabled(true);
}

void DownloadHistoryTab::saveHistory() const {
    saveHistoryToPath(m_historyFilePath, m_historyItems);
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
        saveHistoryToPath(path, items);
    }));
}

void DownloadHistoryTab::addHistoryItem(const HistoryItemData &data) {
    m_historyItems.insert(0, data);
    if (m_historyItems.size() > 500) {
        m_historyItems.removeLast();
    }
    saveHistoryAsync();

    DownloadHistoryItemWidget *itemWidget = new DownloadHistoryItemWidget(data, m_scrollWidget);
    // Insert at index 0 so newest items appear at the top
    m_listLayout->insertWidget(0, itemWidget);

    while (m_listLayout->count() > 501) { // 500 max widgets + 1 stretch
        QLayoutItem *item = m_listLayout->takeAt(m_listLayout->count() - 2);
        if (item && item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

void DownloadHistoryTab::clearHistory() {
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
