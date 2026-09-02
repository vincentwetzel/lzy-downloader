#include "DownloadItemWidget.h"
#include "DownloadItemWidgetIcons.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QStyle>
#include <QStyleOption>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QPropertyAnimation>
#include <QThread>
#include <QTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QMap>
#include <QPair>
#include <QtMath>
#include <utility>

using DownloadItemWidgetIcons::createColoredIcon;

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

void DownloadItemWidget::setThumbnail(const QString &imagePath) {
    if (imagePath.isEmpty() || imagePath == m_currentThumbnailPath) {
        return;
    }

    const QUrl imageUrl(imagePath);
    if (imageUrl.isValid() && (imageUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0
                               || imageUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0)) {
        m_currentThumbnailPath = imagePath;
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

    m_currentThumbnailPath = imagePath;

    // Local thumbnails may live on the same slow/removable/network drive as
    // the download. Loading them synchronously here blocks the GUI thread.
    const QSize thumbnailSize = m_thumbnailLabel->size();
    QPointer<DownloadItemWidget> self(this);
    QCoreApplication *application = QCoreApplication::instance();
    QThread *thread = QThread::create([self, application, imagePath, thumbnailSize]() {
        QImage image(imagePath);
        const bool loaded = !image.isNull();
        QImage scaled;
        if (loaded) {
            scaled = image.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        if (!application) {
            return;
        }
        QMetaObject::invokeMethod(application, [self, imagePath, loaded, scaled = std::move(scaled)]() mutable {
            if (!self || self->m_currentThumbnailPath != imagePath) {
                return;
            }
            if (!loaded) {
                self->m_currentThumbnailPath.clear();
                return;
            }
            self->m_thumbnailLabel->setPixmap(QPixmap::fromImage(std::move(scaled)));
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void DownloadItemWidget::setFinalPath(const QString &path) {
    m_itemData[QStringLiteral("final_path")] = path;
    m_openFolderButton->show();
}

void DownloadItemWidget::setFinished(bool success, const QString &message) {
    if (m_progressUpdateTimer) {
        m_progressUpdateTimer->stop();
    }
    m_pendingProgressData.clear();
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
    }
    m_statusLabel->setText(message);
}

void DownloadItemWidget::setCancelled() {
    if (m_progressUpdateTimer) {
        m_progressUpdateTimer->stop();
    }
    m_pendingProgressData.clear();
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
    m_lastDisplayedProgress = -1.0;
    m_pendingProgressData.clear();
    if (m_progressUpdateTimer) {
        m_progressUpdateTimer->stop();
    }

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
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/settings.ini"), QSettings::IniFormat);
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
