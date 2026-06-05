#include "RuntimeSelectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QVariantList>

RuntimeSelectionDialog::RuntimeSelectionDialog(const QVariantMap &info, bool selectVideo, bool selectAudio, bool selectSubs, QWidget *parent)
    : QDialog(parent), m_info(info), m_selectVideo(selectVideo), m_selectAudio(selectAudio), m_selectSubs(selectSubs),
      m_videoList(nullptr), m_audioList(nullptr), m_subsList(nullptr) {
    setupUi();
    populateData();
}

void RuntimeSelectionDialog::setupUi() {
    setWindowTitle(tr("Select Streams & Subtitles"));
    setMinimumSize(600, 400);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QString title = m_info.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) {
        QLabel *titleLabel = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), this);
        titleLabel->setWordWrap(true);
        mainLayout->addWidget(titleLabel);
    }

    QHBoxLayout *listsLayout = new QHBoxLayout();

    if (m_selectVideo) {
        QGroupBox *videoGroup = new QGroupBox(tr("Video Streams"), this);
        QVBoxLayout *vl = new QVBoxLayout(videoGroup);
        m_videoList = new QListWidget(this);
        vl->addWidget(m_videoList);
        listsLayout->addWidget(videoGroup);
    }

    if (m_selectAudio) {
        QGroupBox *audioGroup = new QGroupBox(tr("Audio Streams"), this);
        QVBoxLayout *al = new QVBoxLayout(audioGroup);
        m_audioList = new QListWidget(this);
        al->addWidget(m_audioList);
        listsLayout->addWidget(audioGroup);
    }

    if (m_selectSubs) {
        QGroupBox *subsGroup = new QGroupBox(tr("Subtitles"), this);
        QVBoxLayout *sl = new QVBoxLayout(subsGroup);
        m_subsList = new QListWidget(this);
        sl->addWidget(m_subsList);
        listsLayout->addWidget(subsGroup);
    }

    mainLayout->addLayout(listsLayout, 1);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void RuntimeSelectionDialog::populateData() {
    QVariantList formats = m_info.value(QStringLiteral("formats")).toList();
    QVariantMap subtitles = m_info.value(QStringLiteral("subtitles")).toMap();
    QVariantMap automatic_captions = m_info.value(QStringLiteral("automatic_captions")).toMap();

    if (m_selectVideo && m_videoList) {
        m_videoList->addItem(tr("Default (Use Settings)"));
        m_videoList->item(0)->setData(Qt::UserRole, QString());
        m_videoList->setCurrentRow(0);
        for (const QVariant &f : formats) {
            QVariantMap fmt = f.toMap();
            QString vcodec = fmt.value(QStringLiteral("vcodec")).toString();
            if (vcodec != QStringLiteral("none") && !vcodec.isEmpty()) {
                QString label = QStringLiteral("[%1] %2x%3 (%4fps) - %5").arg(fmt.value(QStringLiteral("format_id")).toString()).arg(fmt.value(QStringLiteral("width")).toInt()).arg(fmt.value(QStringLiteral("height")).toInt()).arg(fmt.value(QStringLiteral("fps")).toDouble(), 0, 'f', 1).arg(vcodec);
                QListWidgetItem *item = new QListWidgetItem(label);
                item->setData(Qt::UserRole, fmt.value(QStringLiteral("format_id")).toString());
                m_videoList->addItem(item);
            }
        }
    }

    if (m_selectAudio && m_audioList) {
        m_audioList->addItem(tr("Default (Use Settings)"));
        m_audioList->item(0)->setData(Qt::UserRole, QString());
        m_audioList->setCurrentRow(0);
        for (const QVariant &f : formats) {
            QVariantMap fmt = f.toMap();
            QString acodec = fmt.value(QStringLiteral("acodec")).toString();
            if (acodec != QStringLiteral("none") && !acodec.isEmpty()) {
                QString label = QStringLiteral("[%1] %2Hz - %3").arg(fmt.value(QStringLiteral("format_id")).toString()).arg(fmt.value(QStringLiteral("asr")).toInt()).arg(acodec);
                QListWidgetItem *item = new QListWidgetItem(label);
                item->setData(Qt::UserRole, fmt.value(QStringLiteral("format_id")).toString());
                m_audioList->addItem(item);
            }
        }
    }

    if (m_selectSubs && m_subsList) {
        QStringList allLangs = subtitles.keys();
        for (const QString& k : automatic_captions.keys()) if (!allLangs.contains(k)) allLangs.append(k);
        allLangs.sort();
        for (const QString &lang : allLangs) {
            QString autoGenText = (automatic_captions.contains(lang) && !subtitles.contains(lang)) ? tr(" (Auto-generated)") : QString();
            QString label = QStringLiteral("%1%2").arg(lang, autoGenText);
            QListWidgetItem *item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, lang);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            m_subsList->addItem(item);
        }
    }
}

QString RuntimeSelectionDialog::getSelectedVideoFormat() const { return (!m_selectVideo || !m_videoList || !m_videoList->currentItem()) ? "" : m_videoList->currentItem()->data(Qt::UserRole).toString(); }
QString RuntimeSelectionDialog::getSelectedAudioFormat() const { return (!m_selectAudio || !m_audioList || !m_audioList->currentItem()) ? "" : m_audioList->currentItem()->data(Qt::UserRole).toString(); }
QStringList RuntimeSelectionDialog::getSelectedSubtitles() const {
    QStringList subs;
    if (m_selectSubs && m_subsList) {
        for (int i = 0; i < m_subsList->count(); ++i) {
            if (m_subsList->item(i)->checkState() == Qt::Checked) subs << m_subsList->item(i)->data(Qt::UserRole).toString();
        }
    }
    return subs;
}