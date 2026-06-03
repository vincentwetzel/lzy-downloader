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
                QNetworkAccessManager *manager = new QNetworkAccessManager(this);
                QNetworkRequest request(QUrl(data.thumbnailPath));
                QNetworkReply *reply = manager->get(request);
                connect(reply, &QNetworkReply::finished, this, [this, thumbnailLabel, reply, manager]() {
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
                    reply->deleteLater();
                    manager->deleteLater();
                });
            } else if (QFileInfo::exists(data.thumbnailPath)) {
                QImageReader reader(data.thumbnailPath);
                reader.setAutoTransform(true);
                QImage img = reader.read();
                if (!img.isNull()) {
                    thumbnailLabel->setPixmap(QPixmap::fromImage(img).scaled(120, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                } else {
                    thumbnailLabel->setText(tr("No Image"));
                }
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
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleLabel->setFont(titleFont);
        titleLabel->setWordWrap(true);
        
        QLabel *urlLabel = new QLabel(data.url, this);
        QFont urlFont = urlLabel->font();
        urlFont.setPointSize(qMax(8, urlFont.pointSize() - 1));
        urlLabel->setFont(urlFont);
        
        QString sizeStr = data.totalBytes > 0 ? QString::number(data.totalBytes / (1024.0 * 1024.0), 'f', 2) + " MB" : tr("Unknown Size");
        
        QString detailsText = QString("%1 • %2").arg(data.timestamp).arg(sizeStr);
        if (!data.duration.isEmpty()) {
            detailsText += QString(" • %1").arg(data.duration);
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
}

void DownloadHistoryTab::loadHistory(const QString &filePath) {
    m_historyFilePath = filePath;
    m_historyItems.clear();
    
    QFile file(m_historyFilePath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject obj = arr[i].toObject();
                HistoryItemData data;
                data.id = obj[QStringLiteral("id")].toString();
                data.title = obj[QStringLiteral("title")].toString();
                data.url = obj[QStringLiteral("url")].toString();
                data.filePath = obj[QStringLiteral("filePath")].toString();
                data.timestamp = obj[QStringLiteral("timestamp")].toString();
                data.thumbnailPath = obj[QStringLiteral("thumbnailPath")].toString();
                data.totalBytes = obj[QStringLiteral("totalBytes")].toVariant().toLongLong();
                data.duration = obj[QStringLiteral("duration")].toString();
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
    
    for (int i = m_historyItems.size() - 1; i >= 0; --i) {
        DownloadHistoryItemWidget *itemWidget = new DownloadHistoryItemWidget(m_historyItems[i], m_scrollWidget);
        m_listLayout->insertWidget(0, itemWidget);
    }
}

void DownloadHistoryTab::saveHistory() const {
    if (m_historyFilePath.isEmpty()) return;
    
    QJsonArray arr;
    for (const HistoryItemData &data : m_historyItems) {
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
    
    QFile file(m_historyFilePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson());
        file.close();
    }
}

void DownloadHistoryTab::addHistoryItem(const HistoryItemData &data) {
    m_historyItems.insert(0, data);
    if (m_historyItems.size() > 500) {
        m_historyItems.removeLast();
    }
    saveHistory();

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
    saveHistory();
    
    QLayoutItem *item;
    while ((item = m_listLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_listLayout->addStretch();
}