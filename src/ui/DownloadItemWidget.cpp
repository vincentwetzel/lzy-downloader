#include "DownloadItemWidget.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QProgressBar>
#include <QPainter>
#include <QApplication>
#include <QStyle>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

static QIcon createColoredIcon(QStyle::StandardPixmap sp, const QColor &color) {
    QPixmap pixmap = QApplication::style()->standardIcon(sp).pixmap(32, 32);
    if (pixmap.isNull()) return QIcon();
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    return QIcon(pixmap);
}

DownloadItemWidget::DownloadItemWidget(const QVariantMap &itemData, QWidget *parent)
    : QWidget(parent), m_itemData(itemData) {
    setupUi();
}

QString DownloadItemWidget::getId() const {
    return m_itemData[QStringLiteral("id")].toString();
}

QVariantMap DownloadItemWidget::getItemData() const {
    return m_itemData;
}

void DownloadItemWidget::setupUi() {
    QHBoxLayout *mainLayout = new QHBoxLayout(this);

    // Thumbnail label on the left side
    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setFixedSize(80, 60);
    m_thumbnailLabel->setStyleSheet(QStringLiteral("QLabel { background-color: palette(mid); border-radius: 4px; }"));
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setToolTip(tr("Thumbnail preview of the media being downloaded."));
    m_thumbnailLabel->setScaledContents(false);

    const QString initialTitle = m_itemData.value(QStringLiteral("title")).toString().trimmed();
    m_titleLabel = new QLabel(initialTitle.isEmpty() ? m_itemData[QStringLiteral("url")].toString() : initialTitle, this);
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setToolTip(tr("The URL or title of the media being downloaded."));

    m_statusLabel = new QLabel(tr("Queued"), this);
    m_statusLabel->setToolTip(tr("Current status of this download."));

    m_progressBar = new ProgressLabelBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setToolTip(tr("Progress for the currently active stream or processing stage."));

    m_overallProgressLabel = new QLabel(tr("Overall progress"), this);
    m_overallProgressLabel->setToolTip(tr("Overall progress across all primary streams in this download."));
    m_overallProgressLabel->hide();

    m_overallProgressBar = new QProgressBar(this);
    m_overallProgressBar->setRange(0, 100);
    m_overallProgressBar->setValue(0);
    m_overallProgressBar->setTextVisible(false);
    m_overallProgressBar->setMaximumHeight(8);
    m_overallProgressBar->setToolTip(tr("Overall progress across all primary streams in this download."));
    m_overallProgressBar->hide();

    m_clearButton = new QPushButton(QStringLiteral("X"), this);
    m_clearButton->setToolTip(tr("Clear this download from the queue."));
    m_clearButton->setFixedSize(20, 20);
    m_clearButton->setStyleSheet(QStringLiteral("QPushButton { font-weight: bold; color: #dc2626; border: none; } QPushButton:hover { background-color: rgba(150,150,150,0.3); }"));
    m_clearButton->hide();

    QVBoxLayout *infoLayout = new QVBoxLayout();
    QHBoxLayout *titleLayout = new QHBoxLayout();
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addWidget(m_clearButton);
    infoLayout->addLayout(titleLayout);
    infoLayout->addWidget(m_statusLabel);
    infoLayout->addWidget(m_progressBar);
    infoLayout->addWidget(m_overallProgressLabel);
    infoLayout->addWidget(m_overallProgressBar);

    m_cancelButton = new QPushButton(this);
    m_cancelButton->setIcon(createColoredIcon(QStyle::SP_MediaStop, QColor(QStringLiteral("#ef4444"))));
    m_cancelButton->setFixedSize(30, 30);
    m_cancelButton->setToolTip(tr("Stop this download."));

    m_finishButton = new QPushButton(this);
    m_finishButton->setIcon(createColoredIcon(QStyle::SP_DialogApplyButton, QColor(QStringLiteral("#10b981"))));
    m_finishButton->setFixedSize(30, 30);
    m_finishButton->setToolTip(tr("Finish Now (Stop streaming and finalize the video)"));
    m_finishButton->hide();

    m_retryButton = new QPushButton(this);
    m_retryButton->setIcon(createColoredIcon(QStyle::SP_BrowserReload, QColor(QStringLiteral("#eab308"))));
    m_retryButton->setFixedSize(30, 30);
    m_retryButton->setToolTip(tr("Retry this failed or cancelled download."));

    m_openFolderButton = new QPushButton(tr("Open Folder"), this);
    m_openFolderButton->setIcon(createColoredIcon(QStyle::SP_DirOpenIcon, QColor(QStringLiteral("#3b82f6"))));
    m_openFolderButton->setToolTip(tr("Open the folder where this file was saved."));

    QPushButton *clearTempButton = new QPushButton(tr("Clear Temp Files"), this);
    clearTempButton->setIcon(createColoredIcon(QStyle::SP_TrashIcon, QColor(QStringLiteral("#64748b"))));
    clearTempButton->setObjectName(QStringLiteral("clearTempButton"));
    clearTempButton->setToolTip(tr("Delete partial download files from disk to free up space."));
    clearTempButton->hide();

    m_retryButton->hide();
    m_openFolderButton->hide();

    QWidget *buttonContainer = new QWidget(this);
    buttonContainer->setFixedWidth(220); // Provides a strict width so the progress bar never resizes

    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_finishButton);
    buttonLayout->addWidget(m_retryButton);
    buttonLayout->addWidget(clearTempButton);
    buttonLayout->addWidget(m_openFolderButton);
    buttonLayout->addStretch();

    m_moveUpButton = new QPushButton(QStringLiteral("▲"), this);
    m_moveUpButton->setToolTip(tr("Move this download up in the queue."));
    m_moveUpButton->setFixedSize(20, 20);

    m_moveDownButton = new QPushButton(QStringLiteral("▼"), this);
    m_moveDownButton->setToolTip(tr("Move this download down in the queue."));
    m_moveDownButton->setFixedSize(20, 20);

    QWidget *moveContainer = new QWidget(this);
    moveContainer->setFixedWidth(25);

    QVBoxLayout *moveLayout = new QVBoxLayout(moveContainer);
    moveLayout->addWidget(m_moveUpButton);
    moveLayout->addWidget(m_moveDownButton);
    moveLayout->setSpacing(0);
    moveLayout->setContentsMargins(0, 0, 5, 0);

    mainLayout->addWidget(moveContainer);
    mainLayout->addWidget(m_thumbnailLabel);
    mainLayout->addLayout(infoLayout, 1);
    mainLayout->addWidget(buttonContainer);

    connect(m_cancelButton, &QPushButton::clicked, this, &DownloadItemWidget::onCancelClicked);
    connect(m_finishButton, &QPushButton::clicked, this, &DownloadItemWidget::onFinishClicked);
    connect(m_retryButton, &QPushButton::clicked, this, &DownloadItemWidget::onRetryClicked);
    connect(m_openFolderButton, &QPushButton::clicked, this, &DownloadItemWidget::onOpenContainingFolderClicked);
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        m_clearButton->setEnabled(false);
        emit cancelRequested(getId()); // Tell the backend to clean up temp files
        emit clearRequested(getId());
    });
    connect(clearTempButton, &QPushButton::clicked, this, [this, clearTempButton]() {
        clearTempButton->setEnabled(false);
        clearTempButton->setText(tr("Files Cleared"));
        if (m_progressBar) {
            m_progressBar->setRange(0, 100);
            m_progressBar->setValue(0);
            m_progressBar->setProgressText(tr("0% (Files Cleared)"));
        }
        emit cancelRequested(getId()); // Tells backend to delete files since it's already stopped
    });
    connect(m_moveUpButton, &QPushButton::clicked, this, &DownloadItemWidget::onMoveUpClicked);
    connect(m_moveDownButton, &QPushButton::clicked, this, &DownloadItemWidget::onMoveDownClicked);

    if (m_itemData.contains(QStringLiteral("thumbnail_path"))) {
        setThumbnail(m_itemData.value(QStringLiteral("thumbnail_path")).toString());
    }
}

void DownloadItemWidget::updateProgress(const QVariantMap &progressData) {
    if (m_isFinished) {
        return; // Ignore delayed progress signals if already finished
    }

    if (progressData.contains(QStringLiteral("title"))) {
        const QString title = progressData[QStringLiteral("title")].toString().trimmed();
        if (!title.isEmpty()) {
            m_titleLabel->setText(title);
            m_itemData[QStringLiteral("title")] = title;
        }
    }

    // Show "Finish Now" button if the download is active and marked as live
    if (m_itemData.value(QStringLiteral("options")).toMap().value(QStringLiteral("is_live"), false).toBool() && !m_isFinished) {
        m_finishButton->show();
    }

    if (progressData.contains(QStringLiteral("status"))) {
        m_statusLabel->setStyleSheet("");
        QString statusText = progressData[QStringLiteral("status")].toString();

        if (statusText == QStringLiteral("Downloading...")) {
            const QString type = m_itemData.value(QStringLiteral("options")).toMap().value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("audio")) {
                statusText = tr("Downloading audio...");
            } else if (type == QStringLiteral("gallery")) {
                statusText = tr("Downloading gallery...");
            }
        }

        m_statusLabel->setText(statusText);

        // Hide the move up/down buttons once the download officially starts (or is paused)
        if (statusText.contains(tr("Queued")) || statusText == tr("Checking for playlist...")) {
            m_moveUpButton->show();
            m_moveDownButton->show();
        } else {
            m_moveUpButton->hide();
            m_moveDownButton->hide();
        }
    }
    if (progressData.contains(QStringLiteral("progress"))) {
        int progress = progressData[QStringLiteral("progress")].toInt();
        if (progress < 0) {
            // Indeterminate state (queued/starting) - colorless/default
            m_progressBar->setRange(0, 0);
            m_progressBar->setStyleSheet("");
            m_progressBar->setProgressText("");
        } else if (progress == 100 && (m_statusLabel->text().contains(QStringLiteral("Processing"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Merging"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Post"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Extracting"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Converting"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Applying"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Fixing"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Verifying"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Moving"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Copying"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text().contains(QStringLiteral("Embedding"), Qt::CaseInsensitive) ||
                                       m_statusLabel->text() == tr("Complete"))) {
            // Still in post-processing / finalizing phase - teal (animated)
            m_progressBar->setRange(0, 0);
            m_progressBar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: #008080; }"));
            m_progressBar->setProgressText(tr("Finalizing..."));
        } else {
            m_progressBar->setRange(0, 100);
            m_progressBar->setValue(progress);

            // Actively downloading - light blue for all active transfers
            m_progressBar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: #3b82f6; }"));

            // Build centered progress text: percentage + size + speed + ETA
            QStringList parts;
            parts << QStringLiteral("%1%").arg(progress);

            if (progressData.contains(QStringLiteral("downloaded_size")) && progressData.contains(QStringLiteral("total_size"))) {
                parts << QStringLiteral("%1/%2").arg(progressData[QStringLiteral("downloaded_size")].toString(), progressData[QStringLiteral("total_size")].toString());
            }
            if (progressData.contains(QStringLiteral("speed"))) {
                parts << progressData[QStringLiteral("speed")].toString();
            }
            if (progressData.contains(QStringLiteral("eta"))) {
            parts << tr("ETA %1").arg(progressData[QStringLiteral("eta")].toString());
            }
            m_progressBar->setProgressText(parts.join(QStringLiteral("  ")));
        }
    }
    if (progressData.contains(QStringLiteral("overall_progress"))) {
        const int overallProgress = qRound(progressData[QStringLiteral("overall_progress")].toDouble());
        m_overallProgressBar->show();
        m_overallProgressLabel->show();
        m_overallProgressBar->setRange(0, 100);
        m_overallProgressBar->setValue(overallProgress);
        m_overallProgressBar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: #64748b; }"));

        QString overallLabel = tr("Overall %1%").arg(overallProgress);
        if (progressData.contains(QStringLiteral("overall_downloaded_size")) && progressData.contains(QStringLiteral("overall_total_size"))) {
            overallLabel = tr("%1  %2/%3").arg(overallLabel, progressData[QStringLiteral("overall_downloaded_size")].toString(), progressData[QStringLiteral("overall_total_size")].toString());
        }
        m_overallProgressLabel->setText(overallLabel);
    } else if (progressData.contains(QStringLiteral("progress")) && progressData[QStringLiteral("progress")].toInt() < 0) {
        m_overallProgressBar->hide();
        m_overallProgressLabel->hide();
    }

    if (progressData.contains(QStringLiteral("thumbnail_path"))) {
        setThumbnail(progressData[QStringLiteral("thumbnail_path")].toString());
    }
}

void DownloadItemWidget::setThumbnail(const QString &imagePath) {
    if (imagePath.isEmpty() || imagePath == m_currentThumbnailPath) {
        return;
    }
    m_currentThumbnailPath = imagePath;

    if (imagePath.startsWith(QStringLiteral("http://")) || imagePath.startsWith(QStringLiteral("https://"))) {
        QNetworkAccessManager *manager = new QNetworkAccessManager(this);
        QNetworkRequest request((QUrl(imagePath)));
        QNetworkReply *reply = manager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
            if (reply->error() == QNetworkReply::NoError) {
                QPixmap pixmap;
                if (pixmap.loadFromData(reply->readAll())) {
                    QPixmap scaled = pixmap.scaled(m_thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    m_thumbnailLabel->setPixmap(scaled);
                }
            }
            reply->deleteLater();
            manager->deleteLater();
        });
        return;
    }

    QFileInfo fileInfo(imagePath);
    if (!fileInfo.exists()) {
        return;
    }

    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        return;
    }

    // Scale the pixmap to fit the label while maintaining aspect ratio
    QPixmap scaled = pixmap.scaled(m_thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_thumbnailLabel->setPixmap(scaled);
}

void DownloadItemWidget::setFinalPath(const QString &path) {
    m_itemData[QStringLiteral("final_path")] = path;
    m_openFolderButton->show();
}

void DownloadItemWidget::setFinished(bool success, const QString &message) {
    m_cancelButton->hide();
    m_finishButton->hide();
    m_moveUpButton->hide();
    m_moveDownButton->hide();
    m_isFinished = true;
    m_isSuccessful = success;
    m_clearButton->show();

    if (!success) {
        m_retryButton->setEnabled(true);
        m_retryButton->setIcon(createColoredIcon(QStyle::SP_BrowserReload, QColor(QStringLiteral("#eab308"))));
        m_retryButton->setToolTip(tr("Retry"));
        m_retryButton->show();
        m_statusLabel->setStyleSheet(QStringLiteral("color: #dc2626;"));
        if (m_progressBar->maximum() == 0) m_progressBar->setRange(0, 100); // Exit indeterminate mode
        m_progressBar->setStyleSheet(QStringLiteral("QProgressBar { color: #dc2626; }"));
        m_progressBar->setProgressText(tr("Download failed"));
        m_overallProgressBar->hide();
        m_overallProgressLabel->hide();

        if (QPushButton *clearTempButton = findChild<QPushButton*>(QStringLiteral("clearTempButton"))) {
            clearTempButton->show();
            clearTempButton->setEnabled(true);
            clearTempButton->setText(tr("Clear Temp Files"));
        }
    } else {
        m_statusLabel->setStyleSheet(QString());
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        m_progressBar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: #22c55e; }"));
        m_progressBar->setProgressText(tr("Complete"));
        if (m_overallProgressBar->isVisible()) {
            m_overallProgressBar->setRange(0, 100);
            m_overallProgressBar->setValue(100);
            m_overallProgressBar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: #94a3b8; }"));
            m_overallProgressLabel->setText(tr("Overall 100%"));
        }
    }
    m_statusLabel->setText(message);
}

void DownloadItemWidget::setCancelled() {
    m_cancelButton->hide();
    m_finishButton->hide();
    m_moveUpButton->hide();
    m_moveDownButton->hide();
    m_retryButton->setEnabled(true);
    m_retryButton->setIcon(createColoredIcon(QStyle::SP_MediaPlay, QColor(QStringLiteral("#22c55e"))));
    m_retryButton->setToolTip(tr("Resume"));
    m_retryButton->show();
    m_isFinished = true;
    m_isSuccessful = false;
    m_clearButton->show();
    m_statusLabel->setStyleSheet(QStringLiteral("color: #dc2626;"));
    m_statusLabel->setText(tr("Stopped"));
    if (m_progressBar->maximum() == 0) m_progressBar->setRange(0, 100); // Exit indeterminate mode
    m_progressBar->setStyleSheet(QStringLiteral("QProgressBar { color: #dc2626; }"));
    m_progressBar->setProgressText(tr("Stopped"));
    m_overallProgressBar->hide();
    m_overallProgressLabel->hide();

    if (QPushButton *clearTempButton = findChild<QPushButton*>(QStringLiteral("clearTempButton"))) {
        clearTempButton->show();
        clearTempButton->setEnabled(true);
        clearTempButton->setText(tr("Clear Temp Files"));
    }
}

void DownloadItemWidget::setPaused(bool paused) {
    m_isPaused = paused;
    if (paused) {
        m_statusLabel->setText(tr("Paused"));
        m_moveUpButton->show();
        m_moveDownButton->show();
    }
}

void DownloadItemWidget::onCancelClicked() {
    showCancellingFeedback();
    emit cancelRequested(getId());
}

void DownloadItemWidget::onRetryClicked() {
    m_retryButton->setEnabled(false);
    if (m_retryButton->toolTip() == tr("Resume")) {
        m_retryButton->setToolTip(tr("Resuming..."));
    } else {
        m_retryButton->setToolTip(tr("Retrying..."));
    }

    // Reset state so it can accept progress updates again
    m_isFinished = false;
    m_isSuccessful = false;
    m_isPaused = false;

    // Restore normal buttons
    m_retryButton->hide();
    m_clearButton->hide();

    if (QPushButton *clearTempButton = findChild<QPushButton*>(QStringLiteral("clearTempButton"))) {
        clearTempButton->hide();
    }

    m_cancelButton->show();
    m_cancelButton->setEnabled(true);
    m_cancelButton->setIcon(createColoredIcon(QStyle::SP_MediaStop, QColor(QStringLiteral("#ef4444"))));
    m_cancelButton->setToolTip(tr("Stop"));

    // Clear red error/stopped stylesheets
    m_statusLabel->setStyleSheet(QString());
    m_progressBar->setStyleSheet(QString());

    emit retryRequested(m_itemData);
}

void DownloadItemWidget::onOpenContainingFolderClicked() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_itemData[QStringLiteral("final_path")].toString()).path()));
}

void DownloadItemWidget::onPauseResumeClicked() {
    showPausingFeedback(!m_isPaused);
    if (m_isPaused) {
        emit unpauseRequested(getId());
    } else {
        emit pauseRequested(getId());
    }
}

void DownloadItemWidget::onMoveUpClicked() {
    emit moveUpRequested(getId());
}

void DownloadItemWidget::onMoveDownClicked() {
    emit moveDownRequested(getId());
}

void DownloadItemWidget::onFinishClicked() {
    if (m_statusLabel) {
        m_statusLabel->setText(tr("Finishing stream..."));
    }
    m_finishButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    emit finishRequested(getId());
}

void DownloadItemWidget::showCancellingFeedback()
{
    if (m_statusLabel) {
        m_statusLabel->setText(tr("Stopping..."));
    }

    // Disable buttons so the user knows the click registered
    if (m_cancelButton) {
        m_cancelButton->setEnabled(false);
        m_cancelButton->setToolTip(tr("Stopping..."));
    }
}

void DownloadItemWidget::showPausingFeedback(bool pausing)
{
    if (m_statusLabel) {
        m_statusLabel->setText(pausing ? tr("Pausing...") : tr("Resuming..."));
    }
}
