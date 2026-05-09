#ifndef DOWNLOADITEMWIDGET_H
#define DOWNLOADITEMWIDGET_H

#include <QWidget>
#include <QVariantMap>
#include <QProgressBar>
#include <QStyleOptionProgressBar>
#include <QPainter>

class QLabel;
class QPushButton;
class QProgressBar;

// Custom progress bar that paints the percentage text centered on the bar
class ProgressLabelBar : public QProgressBar {
    Q_OBJECT
public:
    void showCancellingFeedback();
    void showPausingFeedback(bool pausing);

    explicit ProgressLabelBar(QWidget *parent = nullptr) : QProgressBar(parent) {
        setMinimumHeight(24);
        setTextVisible(false); // We draw our own centered text
    }

    void setProgressText(const QString &text) { m_progressText = text; update(); }
    QString progressText() const { return m_progressText; }

protected:
    void paintEvent(QPaintEvent *event) override {
        QStyleOptionProgressBar opt;
        initStyleOption(&opt);

        QPainter painter(this);
        // Draw the base progress bar (chunk + groove)
        opt.text = QString();
        style()->drawControl(QStyle::CE_ProgressBar, &opt, &painter, this);

        // Draw centered progress text on top
        if (!m_progressText.isEmpty()) {
            painter.setPen(palette().color(QPalette::Text));
            QFont font = painter.font();
            font.setBold(true);
            font.setPointSize(font.pointSize() + 1);
            painter.setFont(font);
            painter.drawText(rect(), Qt::AlignCenter, m_progressText);
        }
    }

private:
    QString m_progressText;
};

class DownloadItemWidget : public QWidget {
    Q_OBJECT

public:
    explicit DownloadItemWidget(const QVariantMap &itemData, QWidget *parent = nullptr);
    QString getId() const;
    QVariantMap getItemData() const;
    void updateProgress(const QVariantMap &progressData);
    void setFinalPath(const QString &path);
    void setFinished(bool success, const QString &message);
    void setCancelled();

signals:
    void cancelRequested(const QString &id);
    void retryRequested(const QVariantMap &itemData);
    void resumeRequested(const QVariantMap &itemData);
    void clearRequested(const QString &id);
    void pauseRequested(const QString &id);
    void unpauseRequested(const QString &id);
    void moveUpRequested(const QString &id);
    void moveDownRequested(const QString &id);
    void finishRequested(const QString &id);

private slots:
    void onCancelClicked();
    void onRetryClicked();
    void onOpenContainingFolderClicked();
    void onPauseResumeClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onFinishClicked();

private:
    void setupUi();
    void setThumbnail(const QString &imagePath);

    QVariantMap m_itemData;
    QLabel *m_thumbnailLabel;
    QLabel *m_titleLabel;
    QLabel *m_statusLabel;
    ProgressLabelBar *m_progressBar;
    QProgressBar *m_overallProgressBar;
    QLabel *m_overallProgressLabel;
    QPushButton *m_clearButton;
    QPushButton *m_pauseResumeButton;
    QPushButton *m_cancelButton;
    QPushButton *m_retryButton;
    QPushButton *m_openFolderButton;
    QPushButton *m_finishButton;
    QPushButton *m_moveUpButton;
    QPushButton *m_moveDownButton;
    bool m_isFinished = false;
    bool m_isSuccessful = false;
    bool m_isPaused = false;

public:
    void setPaused(bool paused);
    bool isFinished() const { return m_isFinished; }
    bool isSuccessful() const { return m_isSuccessful; }
    void showCancellingFeedback();
    void showPausingFeedback(bool pausing);
};

#endif // DOWNLOADITEMWIDGET_H
