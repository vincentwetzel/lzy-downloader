#include "FormatSelectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QVariantList>
#include <QCheckBox>
#include <QTableWidget>

FormatSelectionDialog::FormatSelectionDialog(const QVariantMap &infoDict, const QVariantMap &options, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Select Format"));
    resize(800, 400);

    setupUI();
    populateTable(infoDict, options.value(QStringLiteral("type"), QStringLiteral("video")).toString());
}

FormatSelectionDialog::~FormatSelectionDialog() {}

void FormatSelectionDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *hintLabel = new QLabel(tr("Check every format you want to download, then click OK to enqueue them all."), this);
    hintLabel->setToolTip(tr("Each selected format will be added to the queue as its own download."));
    mainLayout->addWidget(hintLabel);

    m_table = new QTableWidget(0, 8, this);
    m_table->setHorizontalHeaderLabels({tr("Download"), tr("ID"), tr("Ext"), tr("Resolution"), tr("FPS"), tr("Video Codec"), tr("Audio Codec"), tr("Size")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int column = 1; column < 7; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setToolTip(tr("Choose every format you want to download. Each selected row will be enqueued separately."));
    mainLayout->addWidget(m_table);

    m_selectionSummary = new QLabel(tr("0 formats selected"), this);
    m_selectionSummary->setToolTip(tr("Shows how many formats will be queued when you click OK."));
    mainLayout->addWidget(m_selectionSummary);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(tr("OK"), this);
    QPushButton *cancelButton = new QPushButton(tr("Cancel"), this);
    okButton->setToolTip(tr("Queue the selected formats."));
    cancelButton->setToolTip(tr("Close this window without adding any formats."));
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_table, &QTableWidget::itemChanged, this, &FormatSelectionDialog::onSelectionChanged);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void FormatSelectionDialog::populateTable(const QVariantMap &infoDict, const QString& downloadType) {
    if (!infoDict.contains("formats")) return;

    QVariantList formats = infoDict.value("formats").toList();
    
    // formats in yt-dlp are generally from worst to best, so we'll iterate backwards to put best on top
    for (int i = formats.size() - 1; i >= 0; --i) {
        QVariantMap format = formats[i].toMap();
        
        QString formatId = format.value("format_id").toString();
        QString ext = format.value("ext").toString();
        QString resolution = format.value("resolution").toString();
        if (resolution == "audio only") resolution = "Audio";
        
        QString fps = format.contains("fps") && !format.value("fps").isNull() ? format.value("fps").toString() : "";
        QString vcodec = format.value("vcodec").toString();
        if (vcodec == "none") vcodec = "";
        QString acodec = format.value("acodec").toString();
        if (acodec == "none") acodec = "";
        
        double filesize = format.value("filesize").toDouble();
        if (filesize == 0) filesize = format.value("filesize_approx").toDouble();
        
        QString sizeStr;
        if (filesize > 0) {
            sizeStr = QString::number(filesize / (1024 * 1024), 'f', 2) + " MB";
        } else {
            sizeStr = "Unknown";
        }

        // Filter out video formats if audio-only download is selected
        if (downloadType == "audio" && !vcodec.isEmpty()) {
            continue; 
        }

        int row = m_table->rowCount();
        m_table->insertRow(row);
        QWidget *checkWidget = new QWidget(this);
        checkWidget->setToolTip("Check to enqueue this format.");
        QHBoxLayout *checkLayout = new QHBoxLayout(checkWidget);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setSpacing(0);
        QCheckBox *checkBox = new QCheckBox(checkWidget);
        checkBox->setToolTip("Check to enqueue this format.");
        checkBox->setProperty("formatId", formatId);
        checkLayout->addStretch();
        checkLayout->addWidget(checkBox);
        checkLayout->addStretch();
        m_table->setCellWidget(row, 0, checkWidget);
        m_table->setItem(row, 1, new QTableWidgetItem(formatId));
        m_table->setItem(row, 2, new QTableWidgetItem(ext));
        m_table->setItem(row, 3, new QTableWidgetItem(resolution));
        m_table->setItem(row, 4, new QTableWidgetItem(fps));
        m_table->setItem(row, 5, new QTableWidgetItem(vcodec));
        m_table->setItem(row, 6, new QTableWidgetItem(acodec));
        m_table->setItem(row, 7, new QTableWidgetItem(sizeStr));
        
        for (int column = 1; column < m_table->columnCount(); ++column) {
            if (QTableWidgetItem *item = m_table->item(row, column)) {
                item->setToolTip("This format can be selected using the checkbox in the first column.");
            }
        }

        connect(checkBox, &QCheckBox::checkStateChanged, this, [this]() {
            onSelectionChanged();
        });
    }
}

void FormatSelectionDialog::onSelectionChanged() {
    const int selectedCount = getSelectedFormatIds().size();
    m_selectionSummary->setText(tr("%n format(s) selected", "", selectedCount));
}

QStringList FormatSelectionDialog::getSelectedFormatIds() const {
    QStringList formatIds;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (QWidget *checkWidget = m_table->cellWidget(row, 0)) {
            if (QCheckBox *checkBox = checkWidget->findChild<QCheckBox*>()) {
                if (checkBox->isChecked()) {
                    formatIds.append(checkBox->property("formatId").toString());
                }
            }
        }
    }
    formatIds.removeDuplicates();
    return formatIds;
}
