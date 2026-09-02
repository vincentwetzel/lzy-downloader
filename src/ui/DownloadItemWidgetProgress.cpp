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
#include <QStyle>
#include <QStyleOption>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QPropertyAnimation>
#include <QSettings>
#include <QStandardPaths>
#include <QMap>
#include <QPair>
#include <QTimer>
#include <QtMath>

using DownloadItemWidgetIcons::createColoredIcon;
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
    // Rich-text labels otherwise report the unwrapped title width as their
    // minimum size.  That makes the row wider than the viewport on compact
    // windows and pushes its action buttons off-screen.
    m_titleLabel->setMinimumWidth(0);
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_titleLabel->setToolTip(tr("The URL or title of the media being downloaded."));

    QString displayTitle = initialTitle.isEmpty() ? url : initialTitle;
    QString escapedTitle = displayTitle.toHtmlEscaped();
    if (url.isEmpty()) {
        m_titleLabel->setText(escapedTitle);
    } else {
        m_titleLabel->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), escapedTitle));
    }

    m_statusLabel = new QLabel(tr("Queued"), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumWidth(0);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusLabel->setToolTip(tr("Current status of this download."));

    m_progressBar = new ProgressLabelBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setMinimumWidth(0);
    m_progressBar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_progressBar->setToolTip(tr("Progress for the currently active stream or processing stage."));

    // yt-dlp can emit progress much faster than a QWidget row can repaint.
    // Keep only the newest values and render at a bounded rate so download
    // output cannot starve the GUI event loop.
    m_progressUpdateTimer = new QTimer(this);
    m_progressUpdateTimer->setSingleShot(true);
    m_progressUpdateTimer->setInterval(100);
    connect(m_progressUpdateTimer, &QTimer::timeout, this, &DownloadItemWidget::applyPendingProgress);

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
    buttonContainer->setMinimumWidth(0);
    buttonContainer->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

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
    mainLayout->setStretch(2, 1);
    setMinimumSize(0, 0);
    // The row is hosted by a horizontal-scroll-disabled viewport.  It must
    // be permitted to shrink below its title's natural size so the action
    // buttons remain inside the visible window.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

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

    for (auto it = progressData.constBegin(); it != progressData.constEnd(); ++it) {
        m_pendingProgressData.insert(it.key(), it.value());
    }
    if (m_progressUpdateTimer && !m_progressUpdateTimer->isActive()) {
        m_progressUpdateTimer->start();
    }
}

void DownloadItemWidget::applyPendingProgress() {
    if (m_isFinished) {
        m_pendingProgressData.clear();
        return;
    }

    const QVariantMap progressData = m_pendingProgressData;
    m_pendingProgressData.clear();
    applyProgressData(progressData);
}

void DownloadItemWidget::applyProgressData(const QVariantMap &progressData) {

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

    bool isLive = m_itemData.value(QStringLiteral("options")).toMap().value(QStringLiteral("is_live"), false).toBool();
    if (progressData.contains(QStringLiteral("is_live"))) {
        isLive = progressData.value(QStringLiteral("is_live")).toBool();
        QVariantMap options = m_itemData.value(QStringLiteral("options")).toMap();
        options.insert(QStringLiteral("is_live"), isLive);
        m_itemData.insert(QStringLiteral("options"), options);
    }

    // Show "Finish Now" button if the download is active and marked as live
    if (isLive && !m_isFinished) {
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
        const double streamProgress = progressData.value(QStringLiteral("progress")).toDouble();
        // A multi-format download reports the active stream percentage, which
        // naturally resets when yt-dlp switches from video to audio. Prefer
        // the worker's aggregate value for the desktop bar so it remains
        // monotonic across those stream transitions.
        const bool hasOverallProgress = progressData.contains(QStringLiteral("overall_progress"))
            && progressData.value(QStringLiteral("overall_progress")).toDouble() >= 0.0;
        const double displayedProgress = hasOverallProgress
            ? progressData.value(QStringLiteral("overall_progress")).toDouble()
            : streamProgress;
        // Native yt-dlp and file polling can report the same stream out of
        // order. Do not let a delayed lower aggregate value move the bar back.
        const double monotonicProgress = qMax(m_lastDisplayedProgress, displayedProgress);
        m_lastDisplayedProgress = monotonicProgress;
        int progress = qRound(monotonicProgress);
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
    if (progressData.contains(QStringLiteral("thumbnail_path"))) {
        setThumbnail(progressData[QStringLiteral("thumbnail_path")].toString());
    }
}
