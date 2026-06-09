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
#include <QPropertyAnimation>
#include <QSettings>
#include <QStandardPaths>
#include <QMap>
#include <QPair>

static QIcon createColoredIcon(QStyle::StandardPixmap sp, const QColor &color) {
    static QMap<QPair<int, QRgb>, QIcon> cache;
    QPair<int, QRgb> key = qMakePair(static_cast<int>(sp), color.rgba());
    if (cache.contains(key)) {
        return cache.value(key);
    }

    QPixmap pixmap = QApplication::style()->standardIcon(sp).pixmap(32, 32);
    if (pixmap.isNull()) return QIcon();
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    QIcon icon(pixmap);
    cache.insert(key, icon);
    return icon;
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
    const QString url = m_itemData.value(QStringLiteral("url")).toString();
    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextFormat(Qt::RichText);
    m_titleLabel->setOpenExternalLinks(true);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setToolTip(tr("The URL or title of the media being downloaded."));

    QString displayTitle = initialTitle.isEmpty() ? url : initialTitle;
    QString escapedTitle = displayTitle.toHtmlEscaped();
    if (url.isEmpty()) {
        m_titleLabel->setText(escapedTitle);
    } else {
        m_titleLabel->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), escapedTitle));
    }

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

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setIcon(createColoredIcon(QStyle::SP_MediaStop, QColor(QStringLiteral("#ef4444"))));
    m_cancelButton->setToolTip(tr("Cancel this download and discard any partially downloaded files."));

    m_finishButton = new QPushButton(tr("Stop && Save"), this);
    m_finishButton->setIcon(createColoredIcon(QStyle::SP_DialogApplyButton, QColor(QStringLiteral("#10b981"))));
    m_finishButton->setToolTip(tr("Stop recording this livestream and save the captured video."));
    m_finishButton->hide();

    m_retryButton = new QPushButton(tr("Retry"), this);
    m_retryButton->setIcon(createColoredIcon(QStyle::SP_BrowserReload, QColor(QStringLiteral("#eab308"))));
    m_retryButton->setToolTip(tr("Retry this failed or cancelled download."));

    m_openFolderButton = new QPushButton(tr("Open Folder"), this);
    m_openFolderButton->setIcon(createColoredIcon(QStyle::SP_DirOpenIcon, QColor(QStringLiteral("#3b82f6"))));
    m_openFolderButton->setToolTip(tr("Open the folder where this file was saved."));

    // Use a more compact name for this button
    QPushButton *clearTempButton = new QPushButton(tr("Clear Temp"), this);
    clearTempButton->setIcon(createColoredIcon(QStyle::SP_TrashIcon, QColor(QStringLiteral("#64748b"))));
    clearTempButton->setObjectName(QStringLiteral("clearTempButton"));
    clearTempButton->setToolTip(tr("Delete partial download files from disk to free up space."));
    clearTempButton->hide();

    m_retryButton->hide();
    m_openFolderButton->hide();

    QWidget *buttonContainer = new QWidget(this);

    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(m_finishButton);
    buttonLayout->addWidget(m_cancelButton);
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
        emit clearRequested(getId());
    });
    connect(clearTempButton, &QPushButton::clicked, this, [this, clearTempButton]() {
        clearTempButton->setEnabled(false);
        clearTempButton->setText(tr("Files Cleared"));
        if (m_progressBar) {
            if (QPropertyAnimation *anim = m_progressBar->findChild<QPropertyAnimation*>(QStringLiteral("progressAnim"))) {
                anim->stop();
            }
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
            const QString url = m_itemData.value(QStringLiteral("url")).toString();
            QString escapedTitle = title.toHtmlEscaped();
            if (url.isEmpty()) {
                m_titleLabel->setText(escapedTitle);
            } else {
                m_titleLabel->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), escapedTitle));
            }
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
            if (QPropertyAnimation *anim = m_progressBar->findChild<QPropertyAnimation*>(QStringLiteral("progressAnim"))) {
                anim->stop();
            }
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
            if (QPropertyAnimation *anim = m_progressBar->findChild<QPropertyAnimation*>(QStringLiteral("progressAnim"))) {
                anim->stop();
            }
            m_progressBar->setRange(0, 0);
            m_progressBar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: #008080; }"));
            m_progressBar->setProgressText(tr("Finalizing..."));
        } else {
            m_progressBar->setRange(0, 100);
            QPropertyAnimation *anim = m_progressBar->findChild<QPropertyAnimation*>(QStringLiteral("progressAnim"));
            if (!anim) {
                anim = new QPropertyAnimation(m_progressBar, QByteArrayLiteral("value"), m_progressBar);
                anim->setObjectName(QStringLiteral("progressAnim"));
                anim->setEasingCurve(QEasingCurve::OutQuad);
            }
            anim->stop();
            anim->setDuration(300);
            anim->setStartValue(m_progressBar->value());
            anim->setEndValue(progress);
            anim->start();

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
        QPropertyAnimation *anim = m_overallProgressBar->findChild<QPropertyAnimation*>(QStringLiteral("overallProgressAnim"));
        if (!anim) {
            anim = new QPropertyAnimation(m_overallProgressBar, QByteArrayLiteral("value"), m_overallProgressBar);
            anim->setObjectName(QStringLiteral("overallProgressAnim"));
            anim->setEasingCurve(QEasingCurve::OutQuad);
        }
        anim->stop();
        anim->setDuration(300);
        anim->setStartValue(m_overallProgressBar->value());
        anim->setEndValue(overallProgress);
        anim->start();
        m_overallProgressBar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: #64748b; }"));

        QString overallLabel = tr("Overall %1%").arg(overallProgress);
        if (progressData.contains(QStringLiteral("overall_downloaded_size")) && progressData.contains(QStringLiteral("overall_total_size"))) {
            overallLabel = QStringLiteral("%1  %2/%3").arg(overallLabel, progressData[QStringLiteral("overall_downloaded_size")].toString(), progressData[QStringLiteral("overall_total_size")].toString());
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
        QNetworkAccessManager *manager = qApp->findChild<QNetworkAccessManager*>(QStringLiteral("sharedThumbnailManager"));
        if (!manager) {
            manager = new QNetworkAccessManager(qApp);
            manager->setObjectName(QStringLiteral("sharedThumbnailManager"));
        }
        QNetworkRequest request{QUrl(imagePath)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LzyDownloader"));
        request.setTransferTimeout(15000);
        QNetworkReply *reply = manager->get(request);
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                QPixmap pixmap;
                if (pixmap.loadFromData(reply->readAll())) {
                    QPixmap scaled = pixmap.scaled(m_thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    m_thumbnailLabel->setPixmap(scaled);
                }
            }
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
        m_retryButton->setText(tr("Retry"));
        m_retryButton->setIcon(createColoredIcon(QStyle::SP_BrowserReload, QColor(QStringLiteral("#eab308"))));
        m_retryButton->setToolTip(tr("Retry this failed download."));
        m_retryButton->show();
        m_statusLabel->setStyleSheet(QStringLiteral("color: #dc2626;"));
        if (m_progressBar->maximum() == 0) m_progressBar->setRange(0, 100); // Exit indeterminate mode
        m_progressBar->setStyleSheet(QStringLiteral("QProgressBar { color: #dc2626; } QProgressBar::chunk { background-color: #dc2626; }"));
        m_progressBar->setProgressText(tr("Failed"));
        m_overallProgressBar->hide();
        m_overallProgressLabel->hide();

        if (QPushButton *clearTempButton = findChild<QPushButton*>(QStringLiteral("clearTempButton"))) {
            if (hasAssociatedTemporaryFiles()) {
                clearTempButton->show();
                clearTempButton->setEnabled(true);
                clearTempButton->setText(tr("Clear Temp"));
            } else {
                clearTempButton->hide();
            }
        }
    } else {
        m_statusLabel->setStyleSheet(QString());
        if (QPropertyAnimation *anim = m_progressBar->findChild<QPropertyAnimation*>(QStringLiteral("progressAnim"))) {
            anim->stop();
        }
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        m_progressBar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: #22c55e; }"));
        m_progressBar->setProgressText(tr("Complete"));
        if (m_overallProgressBar->isVisible()) {
            if (QPropertyAnimation *anim = m_overallProgressBar->findChild<QPropertyAnimation*>(QStringLiteral("overallProgressAnim"))) {
                anim->stop();
            }
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
    m_retryButton->setText(tr("Resume"));
    m_retryButton->setIcon(createColoredIcon(QStyle::SP_MediaPlay, QColor(QStringLiteral("#22c55e"))));
    m_retryButton->setToolTip(tr("Resume this download."));
    m_retryButton->show();
    m_isFinished = true;
    m_isSuccessful = false;
    m_clearButton->show();
    m_statusLabel->setStyleSheet(QStringLiteral("color: #dc2626;"));
    m_statusLabel->setText(tr("Cancelled"));
    if (QPropertyAnimation *anim = m_progressBar->findChild<QPropertyAnimation*>(QStringLiteral("progressAnim"))) {
        anim->stop();
    }
    if (m_progressBar->maximum() == 0) m_progressBar->setRange(0, 100); // Exit indeterminate mode
    m_progressBar->setStyleSheet(QStringLiteral("QProgressBar { color: #dc2626; } QProgressBar::chunk { background-color: #dc2626; }"));
    m_progressBar->setProgressText(tr("Cancelled"));
    m_overallProgressBar->hide();
    m_overallProgressLabel->hide();

    if (QPushButton *clearTempButton = findChild<QPushButton*>(QStringLiteral("clearTempButton"))) {
        if (hasAssociatedTemporaryFiles()) {
            clearTempButton->show();
            clearTempButton->setEnabled(true);
            clearTempButton->setText(tr("Clear Temp"));
        } else {
            clearTempButton->hide();
        }
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
    if (m_retryButton->text() == tr("Resume")) {
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
    m_cancelButton->setToolTip(tr("Cancel this download and discard any partially downloaded files."));

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
        m_statusLabel->setText(tr("Stopping && Saving..."));
    }
    m_finishButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    emit finishRequested(getId());
}

void DownloadItemWidget::showCancellingFeedback()
{
    if (m_statusLabel) {
        m_statusLabel->setText(tr("Cancelling..."));
    }

    // Disable buttons so the user knows the click registered
    if (m_cancelButton) {
        m_cancelButton->setEnabled(false);
        m_cancelButton->setToolTip(tr("Cancelling..."));
    }
}

void DownloadItemWidget::showPausingFeedback(bool pausing)
{
    if (m_statusLabel) {
        m_statusLabel->setText(pausing ? tr("Pausing...") : tr("Resuming..."));
    }
}

bool DownloadItemWidget::hasAssociatedTemporaryFiles() const {
    const QString id = getId();
    if (id.isEmpty()) {
        return false;
    }

    // 1. Check the standard temporary downloads directory directly using the download's ID
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    QString tempDirStr = settings.value(QStringLiteral("Paths/temporary_downloads_directory")).toString();
    if (tempDirStr.isEmpty()) {
        const QString completedDir = settings.value(QStringLiteral("Paths/completed_downloads_directory")).toString();
        if (!completedDir.isEmpty()) {
            tempDirStr = QDir(completedDir).filePath(QStringLiteral("temp_downloads"));
        }
    }

    if (!tempDirStr.isEmpty()) {
        QDir uuidDir(QDir(tempDirStr).filePath(id));
        if (uuidDir.exists() && uuidDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).count() > 0) {
            return true;
        }
    }

    // 2. Fallback to check tempFilePath if populated
    const QString tempPath = m_itemData.value(QStringLiteral("tempFilePath")).toString();
    if (!tempPath.isEmpty()) {
        QFileInfo info(tempPath);
        if (info.exists()) {
            if (info.isDir()) {
                QDir dir(tempPath);
                if (dir.dirName() == id && dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).count() > 0) {
                    return true;
                }
            } else {
                return true;
            }
        } else {
            QDir dir(info.absolutePath());
            if (dir.dirName() == id && dir.exists() && dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).count() > 0) {
                return true;
            }
        }
    }

    // 3. Fallback to check originalDownloadedFilePath if populated
    const QString origPath = m_itemData.value(QStringLiteral("originalDownloadedFilePath")).toString();
    if (!origPath.isEmpty()) {
        QFileInfo info(origPath);
        if (info.exists()) {
            if (info.isDir()) {
                QDir dir(origPath);
                if (dir.dirName() == id && dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).count() > 0) {
                    return true;
                }
            } else {
                return true;
            }
        } else {
            QDir dir(info.absolutePath());
            if (dir.dirName() == id && dir.exists() && dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).count() > 0) {
                return true;
            }
        }
    }

    // 4. Fallback to check cleanup_candidates if populated
    const QStringList cleanupCandidates = m_itemData.value(QStringLiteral("cleanup_candidates")).toStringList();
    for (const QString &candidate : cleanupCandidates) {
        if (!candidate.isEmpty()) {
            QFileInfo info(candidate);
            if (info.exists()) {
                return true;
            } else {
                QDir dir(info.absolutePath());
                if (dir.dirName() == id && dir.exists() && dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).count() > 0) {
                    return true;
                }
            }
        }
    }

    return false;
}
