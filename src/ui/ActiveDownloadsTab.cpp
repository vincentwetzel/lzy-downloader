#include "ActiveDownloadsTab.h"
#include "DownloadItemWidget.h"
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QScrollArea>
#include <QLabel>
#include <QDebug>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QDesktopServices>
#include <QDir>
#include <QApplication>
#include <QStyle>
#include "core/ConfigManager.h"
#include <QPainter>
#include <chrono>

static QIcon createColoredIcon(QStyle::StandardPixmap sp, const QColor &color) {
    QPixmap pixmap = QApplication::style()->standardIcon(sp).pixmap(32, 32);
    if (pixmap.isNull()) return QIcon();
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    return QIcon(pixmap);
}

ActiveDownloadsTab::ActiveDownloadsTab(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    setupUi();
}

void ActiveDownloadsTab::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    QHBoxLayout *toolbarLayout = new QHBoxLayout();
    m_cancelAllButton = new QPushButton(this);
    m_cancelAllButton->setIcon(createColoredIcon(QStyle::SP_MediaStop, QColor(QStringLiteral("#ef4444"))));
    m_cancelAllButton->setToolTip(tr("Stop All Active Downloads"));
    m_cancelAllButton->setFixedSize(32, 32);

    QPushButton *resumeAllButton = new QPushButton(this);
    resumeAllButton->setIcon(createColoredIcon(QStyle::SP_MediaPlay, QColor(QStringLiteral("#22c55e"))));
    resumeAllButton->setObjectName(QStringLiteral("resumeAllButton"));
    resumeAllButton->setToolTip(tr("Resume all stopped or failed downloads."));
    resumeAllButton->setFixedSize(32, 32);
    connect(resumeAllButton, &QPushButton::clicked, this, [this]() {
        QStringList idsToResume;
        for (int i = 0; i < m_downloadsLayout->count(); ++i) {
            if (QWidget *widget = m_downloadsLayout->itemAt(i)->widget()) {
                if (DownloadItemWidget *itemWidget = qobject_cast<DownloadItemWidget*>(widget)) {
                    if (itemWidget->isFinished() && !itemWidget->isSuccessful()) {
                        idsToResume.append(itemWidget->getId());
                    }
                }
            }
        }
        
        if (idsToResume.isEmpty()) return;

        if (QMessageBox::question(this, tr("Resume All"), tr("Are you sure you want to resume all stopped or failed downloads?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
            return;
        }

        for (const QString &id : idsToResume) {
            if (m_downloadItems.contains(id)) {
                DownloadItemWidget *itemWidget = m_downloadItems[id];
                for (QPushButton *btn : itemWidget->findChildren<QPushButton*>()) {
                    if (btn->toolTip() == tr("Resume") || btn->toolTip() == tr("Retry")) {
                        btn->click();
                        break;
                    }
                }
            }
        }
    });

    m_clearInactiveButton = new QPushButton(this);
    m_clearInactiveButton->setIcon(createColoredIcon(QStyle::SP_TrashIcon, QColor(QStringLiteral("#64748b"))));
    m_clearInactiveButton->setToolTip(tr("Clear all inactive (completed, stopped, and failed) downloads."));
    m_clearInactiveButton->setFixedSize(32, 32);
    
    connect(m_clearInactiveButton, &QPushButton::clicked, this, [this]() {
        QStringList completedToRemove;
        QStringList incompleteToRemove;
        
        for (int i = 0; i < m_downloadsLayout->count(); ++i) {
            if (QWidget *widget = m_downloadsLayout->itemAt(i)->widget()) {
                if (DownloadItemWidget *itemWidget = qobject_cast<DownloadItemWidget*>(widget)) {
                    if (itemWidget->isFinished()) {
                        if (itemWidget->isSuccessful()) {
                            completedToRemove.append(itemWidget->getId());
                        } else {
                            incompleteToRemove.append(itemWidget->getId());
                        }
                    }
                }
            }
        }
        
        bool deleteTempFiles = false;
        if (!incompleteToRemove.isEmpty()) {
            deleteTempFiles = (QMessageBox::question(this, tr("Clear Inactive Downloads"),
                                                     tr("Do you also want to delete temporary files for the incomplete downloads being cleared?"),
                                                     QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes);
        }

        for (const QString &id : completedToRemove) {
            onItemClearRequested(id);
        }
        for (const QString &id : incompleteToRemove) {
            if (deleteTempFiles) {
                emit cancelDownloadRequested(id);
            }
            onItemClearRequested(id);
        }
    });

    // Add folder buttons for quick access to download directories
    QPushButton *openTempFolderButton = new QPushButton(tr("Open Temporary Folder"), this);
    openTempFolderButton->setIcon(createColoredIcon(QStyle::SP_DirIcon, QColor(QStringLiteral("#3b82f6"))));
    openTempFolderButton->setToolTip(tr("Click here to open the folder where active downloads are temporarily stored."));
    connect(openTempFolderButton, &QPushButton::clicked, this, [this]() {
        QString tempDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("temporary_downloads_directory")).toString();
        if (tempDir.isEmpty() || !QDir(tempDir).exists()) {
            QMessageBox::warning(this, tr("Folder Not Found"),
                                 tr("The temporary downloads directory is not set or does not exist.\n"
                                 "Please configure it in the Advanced Settings tab."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(tempDir));
    });
    
    QPushButton *openDownloadsFolderButton = new QPushButton(tr("Open Downloads Folder"), this);
    openDownloadsFolderButton->setIcon(createColoredIcon(QStyle::SP_DirOpenIcon, QColor(QStringLiteral("#3b82f6"))));
    openDownloadsFolderButton->setToolTip(tr("Click here to open the folder where all your finished downloads are saved."));
    connect(openDownloadsFolderButton, &QPushButton::clicked, this, [this]() {
        QString downloadsDir = m_configManager->get(QStringLiteral("Paths"), QStringLiteral("completed_downloads_directory")).toString();
        if (downloadsDir.isEmpty() || !QDir(downloadsDir).exists()) {
            QMessageBox::warning(this, tr("Folder Not Found"),
                                 tr("The downloads directory is not set or does not exist.\n"
                                 "Please configure it in the Advanced Settings tab."));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(downloadsDir));
    });
    
    toolbarLayout->addWidget(m_cancelAllButton);
    toolbarLayout->addWidget(resumeAllButton);
    toolbarLayout->addWidget(m_clearInactiveButton);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(openTempFolderButton);
    toolbarLayout->addWidget(openDownloadsFolderButton);
    mainLayout->addLayout(toolbarLayout);

    QWidget *stackedContainer = new QWidget(this);
    m_stackedLayout = new QStackedLayout(stackedContainer);
    mainLayout->addWidget(stackedContainer, 1);

    // Setup Placeholder Widget (Page 0)
    m_placeholderWidget = new QWidget(this);
    QVBoxLayout *placeholderLayout = new QVBoxLayout(m_placeholderWidget);

    QLabel *iconLabel = new QLabel(QStringLiteral("📥"), this);
    iconLabel->setAlignment(Qt::AlignCenter);
    QFont iconFont = iconLabel->font();
    iconFont.setPointSize(48);
    iconLabel->setFont(iconFont);

    QLabel *textLabel = new QLabel(tr("No active downloads.\nAdd a URL in the Start Download tab to begin."), this);
    textLabel->setAlignment(Qt::AlignCenter);
    textLabel->setStyleSheet(QStringLiteral("color: palette(shadow); font-size: 14px;"));

    placeholderLayout->addStretch();
    placeholderLayout->addWidget(iconLabel);
    placeholderLayout->addSpacing(10);
    placeholderLayout->addWidget(textLabel);
    placeholderLayout->addStretch();

    // Setup Downloads Container (Page 1)
    m_downloadsContainer = new QWidget(this);
    m_downloadsLayout = new QVBoxLayout(m_downloadsContainer);
    m_downloadsLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadsLayout->addStretch(); // Push downloads to the top

    // Wrap the downloads container in a QScrollArea
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(m_downloadsContainer);

    // Add widgets to the stacked layout
    m_stackedLayout->addWidget(m_placeholderWidget);
    m_stackedLayout->addWidget(scrollArea);

    updatePlaceholderVisibility();

    connect(m_cancelAllButton, &QPushButton::clicked, this, &ActiveDownloadsTab::cancelAllDownloads);
}

void ActiveDownloadsTab::addDownloadItem(const QVariantMap &itemData) {
    QString id = itemData[QStringLiteral("id")].toString();
    if (m_downloadItems.contains(id)) {
        onItemClearRequested(id);
    }

    // Automatically prune old finished downloads if the list gets too long
    // Defaults to 100 items to prevent UI lag over long sessions
    int maxItems = m_configManager->get(QStringLiteral("General"), QStringLiteral("max_ui_downloads"), 100).toInt();
    if (maxItems > 0 && m_downloadItems.size() >= maxItems) {
        for (int i = 0; i < m_downloadsLayout->count() && m_downloadItems.size() >= maxItems; ) {
            if (QWidget *widget = m_downloadsLayout->itemAt(i)->widget()) {
                if (DownloadItemWidget *itemWidget = qobject_cast<DownloadItemWidget*>(widget)) {
                    if (itemWidget->isFinished()) {
                        onItemClearRequested(itemWidget->getId());
                        continue; // Layout shifted, check the same index again
                    }
                }
            }
            ++i;
        }
    }

    DownloadItemWidget *itemWidget = new DownloadItemWidget(itemData, this);

    // Insert before the stretch
    m_downloadsLayout->insertWidget(m_downloadsLayout->count() - 1, itemWidget);
    m_downloadItems[id] = itemWidget;

    connect(itemWidget, &DownloadItemWidget::cancelRequested, this, &ActiveDownloadsTab::cancelDownloadRequested);
    connect(itemWidget, &DownloadItemWidget::retryRequested, this, &ActiveDownloadsTab::retryDownloadRequested);
    connect(itemWidget, &DownloadItemWidget::resumeRequested, this, &ActiveDownloadsTab::resumeDownloadRequested);
    connect(itemWidget, &DownloadItemWidget::clearRequested, this, &ActiveDownloadsTab::onItemClearRequested);
    connect(itemWidget, &DownloadItemWidget::pauseRequested, this, &ActiveDownloadsTab::pauseDownloadRequested);
    connect(itemWidget, &DownloadItemWidget::unpauseRequested, this, &ActiveDownloadsTab::unpauseDownloadRequested);
    connect(itemWidget, &DownloadItemWidget::moveUpRequested, this, &ActiveDownloadsTab::onItemMoveUpRequested);
    connect(itemWidget, &DownloadItemWidget::moveDownRequested, this, &ActiveDownloadsTab::onItemMoveDownRequested);
    connect(itemWidget, &DownloadItemWidget::finishRequested, this, &ActiveDownloadsTab::finishDownloadRequested);

    updatePlaceholderVisibility();
}

void ActiveDownloadsTab::updateDownloadProgress(const QString &id, const QVariantMap &progressData) {
    if (m_downloadItems.contains(id)) {
        m_downloadItems[id]->updateProgress(progressData);
    }
}

void ActiveDownloadsTab::onDownloadFinished(const QString &id, bool success, const QString &message) {
    if (m_downloadItems.contains(id)) {
        m_downloadItems[id]->setFinished(success, message);
        if (success && m_configManager->get(QStringLiteral("DownloadOptions"), QStringLiteral("auto_clear_completed"), false).toBool()) {
            QTimer::singleShot(std::chrono::seconds(2), this, [this, id]() { onItemClearRequested(id); });
        }
    }
}

void ActiveDownloadsTab::onDownloadCancelled(const QString &id) {
    if (m_downloadItems.contains(id)) {
        m_downloadItems[id]->setCancelled();
    }
}

void ActiveDownloadsTab::onDownloadPaused(const QString &id) {
    if (m_downloadItems.contains(id)) {
        m_downloadItems[id]->setPaused(true);
    }
}

void ActiveDownloadsTab::onDownloadResumed(const QString &id) {
    if (m_downloadItems.contains(id)) {
        m_downloadItems[id]->setPaused(false);
        m_downloadItems[id]->updateProgress({{QStringLiteral("status"), tr("Resuming download...")}, {QStringLiteral("progress"), -1}});
    }
}

void ActiveDownloadsTab::removeDownloadItem(const QString &id) {
    onItemClearRequested(id);
}

void ActiveDownloadsTab::onDownloadFinalPathReady(const QString &id, const QString &path) {
    if (m_downloadItems.contains(id)) {
        m_downloadItems[id]->setFinalPath(path);
    }
}

void ActiveDownloadsTab::setDownloadStatus(const QString &id, const QString &status) {
    if (m_downloadItems.contains(id)) {
        m_downloadItems[id]->updateProgress({{QStringLiteral("status"), status}});
    }
}

void ActiveDownloadsTab::addExpandingPlaylist(const QString &url) {
    // UI for this can be added later
}

void ActiveDownloadsTab::removeExpandingPlaylist(const QString &url, int count) {
    // UI for this can be added later
}

void ActiveDownloadsTab::updatePlaceholderVisibility() {
    if (m_downloadItems.isEmpty()) {
        m_stackedLayout->setCurrentIndex(0); // Show placeholder
        m_cancelAllButton->setEnabled(false);
        m_clearInactiveButton->setEnabled(false);
        if (QPushButton *btn = findChild<QPushButton*>(QStringLiteral("resumeAllButton"))) btn->setEnabled(false);
    } else {
        m_stackedLayout->setCurrentIndex(1); // Show downloads
        m_cancelAllButton->setEnabled(true);
        m_clearInactiveButton->setEnabled(true);
        if (QPushButton *btn = findChild<QPushButton*>(QStringLiteral("resumeAllButton"))) btn->setEnabled(true);
    }
}

void ActiveDownloadsTab::cancelAllDownloads() {
    QStringList toCancel;
    for (int i = 0; i < m_downloadsLayout->count(); ++i) {
        if (QWidget *widget = m_downloadsLayout->itemAt(i)->widget()) {
            if (DownloadItemWidget *itemWidget = qobject_cast<DownloadItemWidget*>(widget)) {
                if (!itemWidget->isFinished()) {
                    toCancel.append(itemWidget->getId());
                }
            }
        }
    }
    
    if (toCancel.isEmpty()) return;

    if (QMessageBox::question(this, tr("Stop All"), tr("Are you sure you want to stop all active downloads?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
        return;
    }

    for (const QString &id : toCancel) {
        if (m_downloadItems.contains(id)) {
            m_downloadItems[id]->showCancellingFeedback();
            emit cancelDownloadRequested(id);
        }
    }
}

void ActiveDownloadsTab::togglePauseAllDownloads() {
    m_isAllPaused = !m_isAllPaused;
    
    QStringList toToggle;
    for (int i = 0; i < m_downloadsLayout->count(); ++i) {
        if (QWidget *widget = m_downloadsLayout->itemAt(i)->widget()) {
            if (DownloadItemWidget *itemWidget = qobject_cast<DownloadItemWidget*>(widget)) {
                if (!itemWidget->isFinished()) {
                    toToggle.append(itemWidget->getId());
                }
            }
        }
    }

    for (const QString &id : toToggle) {
        if (m_downloadItems.contains(id)) {
            m_downloadItems[id]->showPausingFeedback(m_isAllPaused);
            if (m_isAllPaused) {
                emit pauseDownloadRequested(id);
            } else {
                emit unpauseDownloadRequested(id);
            }
        }
    }
}

void ActiveDownloadsTab::onItemClearRequested(const QString &id) {
    if (m_downloadItems.contains(id)) {
        DownloadItemWidget *widget = m_downloadItems.take(id);
        emit itemCleared(id, widget->isSuccessful(), widget->isFinished());
        m_downloadsLayout->removeWidget(widget);
        widget->deleteLater();
        updatePlaceholderVisibility();
    }
}

void ActiveDownloadsTab::onItemMoveUpRequested(const QString &id) {
    if (!m_downloadItems.contains(id)) return;
    DownloadItemWidget *widget = m_downloadItems[id];
    int index = m_downloadsLayout->indexOf(widget);
    if (index > 0) { // Not already at the top
        m_downloadsLayout->removeWidget(widget);
        m_downloadsLayout->insertWidget(index - 1, widget);
        emit moveDownloadUpRequested(id);
    }
}

void ActiveDownloadsTab::onItemMoveDownRequested(const QString &id) {
    if (!m_downloadItems.contains(id)) return;
    DownloadItemWidget *widget = m_downloadItems[id];
    int index = m_downloadsLayout->indexOf(widget);
    // m_downloadsLayout has a stretch at the end, so count() - 2 is the last actual widget
    if (index >= 0 && index < m_downloadsLayout->count() - 2) {
        m_downloadsLayout->removeWidget(widget);
        m_downloadsLayout->insertWidget(index + 1, widget);
        emit moveDownloadDownRequested(id);
    }
}
