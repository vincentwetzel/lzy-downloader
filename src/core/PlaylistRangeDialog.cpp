#include "PlaylistRangeDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QDialogButtonBox>

PlaylistRangeDialog::PlaylistRangeDialog(const QList<QVariantMap> &playlistItems, QWidget *parent)
    : QDialog(parent), m_playlistItems(playlistItems) {
    setupUi();
}

PlaylistRangeDialog::~PlaylistRangeDialog() = default;

void PlaylistRangeDialog::setupUi() {
    setWindowTitle(tr("Select Playlist Items"));
    setMinimumSize(500, 400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *instructions = new QLabel(tr("Enter a range (e.g. 1-5, 8, 11-13) or select individual items below:"), this);
    mainLayout->addWidget(instructions);

    m_rangeEdit = new QLineEdit(this);
    m_rangeEdit->setPlaceholderText(tr("e.g. 1-5, 8, 11-13"));
    mainLayout->addWidget(m_rangeEdit);

    QHBoxLayout *selectionButtonsLayout = new QHBoxLayout();
    QPushButton *selectAllBtn = new QPushButton(tr("Select All"), this);
    QPushButton *selectNoneBtn = new QPushButton(tr("Select None"), this);
    selectionButtonsLayout->addWidget(selectAllBtn);
    selectionButtonsLayout->addWidget(selectNoneBtn);
    selectionButtonsLayout->addStretch();
    mainLayout->addLayout(selectionButtonsLayout);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    
    int i = 0;
    for (const QVariantMap &item : m_playlistItems) {
        QString title = item.value(QStringLiteral("title")).toString();
        if (title.isEmpty()) {
            title = tr("Item %1").arg(i + 1);
        }
        
        QListWidgetItem *listItem = new QListWidgetItem(QStringLiteral("%1. %2").arg(i + 1).arg(title), m_listWidget);
        listItem->setFlags(listItem->flags() | Qt::ItemIsUserCheckable);
        listItem->setCheckState(Qt::Checked); // Default to all selected
        listItem->setData(Qt::UserRole, i);   // Store the original index
        i++;
    }
    mainLayout->addWidget(m_listWidget);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    connect(m_rangeEdit, &QLineEdit::textEdited, this, &PlaylistRangeDialog::onRangeTextChanged);
    connect(m_listWidget, &QListWidget::itemChanged, this, &PlaylistRangeDialog::onItemChanged);
    
    connect(selectAllBtn, &QPushButton::clicked, this, &PlaylistRangeDialog::selectAll);
    connect(selectNoneBtn, &QPushButton::clicked, this, &PlaylistRangeDialog::selectNone);

    // Populate initial text based on the default "Checked" state
    syncRangeTextFromList();
}

void PlaylistRangeDialog::onRangeTextChanged(const QString &text) {
    if (m_isSyncing) return;
    m_isSyncing = true;
    syncListFromRangeText(text);
    m_isSyncing = false;
}

void PlaylistRangeDialog::onItemChanged() {
    if (m_isSyncing) return;
    m_isSyncing = true;
    syncRangeTextFromList();
    m_isSyncing = false;
}

void PlaylistRangeDialog::selectAll() {
    m_isSyncing = true;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        m_listWidget->item(i)->setCheckState(Qt::Checked);
    }
    syncRangeTextFromList();
    m_isSyncing = false;
}

void PlaylistRangeDialog::selectNone() {
    m_isSyncing = true;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        m_listWidget->item(i)->setCheckState(Qt::Unchecked);
    }
    syncRangeTextFromList();
    m_isSyncing = false;
}

void PlaylistRangeDialog::syncListFromRangeText(const QString &text) {
    for (int i = 0; i < m_listWidget->count(); ++i) {
        m_listWidget->item(i)->setCheckState(Qt::Unchecked);
    }
    if (text.trimmed().isEmpty()) return;

    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString p = part.trimmed();
        if (p.contains(QLatin1Char('-'))) {
            const QStringList range = p.split(QLatin1Char('-'));
            if (range.size() == 2) {
                bool ok1 = true, ok2 = true;
                int start = 1, end = m_listWidget->count();
                
                if (!range.at(0).trimmed().isEmpty()) start = range.at(0).trimmed().toInt(&ok1);
                if (!range.at(1).trimmed().isEmpty()) end = range.at(1).trimmed().toInt(&ok2);

                if (ok1 && ok2 && start <= end) {
                    for (int i = start; i <= end; ++i) {
                        if (i >= 1 && i <= m_listWidget->count()) m_listWidget->item(i - 1)->setCheckState(Qt::Checked);
                    }
                }
            }
        } else {
            bool ok;
            const int index = p.toInt(&ok);
            if (ok && index >= 1 && index <= m_listWidget->count()) m_listWidget->item(index - 1)->setCheckState(Qt::Checked);
        }
    }
}

void PlaylistRangeDialog::syncRangeTextFromList() {
    QStringList ranges;
    int rangeStart = -1;
    
    for (int i = 0; i <= m_listWidget->count(); ++i) {
        const bool checked = (i < m_listWidget->count()) && (m_listWidget->item(i)->checkState() == Qt::Checked);
        if (checked) {
            if (rangeStart == -1) rangeStart = i + 1; // 1-based index
        } else if (rangeStart != -1) {
            int rangeEnd = i;
            ranges.append(rangeStart == rangeEnd ? QString::number(rangeStart) : QStringLiteral("%1-%2").arg(rangeStart).arg(rangeEnd));
            rangeStart = -1;
        }
    }
    m_rangeEdit->setText(ranges.join(QStringLiteral(", ")));
}

QList<QVariantMap> PlaylistRangeDialog::getSelectedItems() const {
    QList<QVariantMap> selected;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        if (m_listWidget->item(i)->checkState() == Qt::Checked) {
            selected.append(m_playlistItems.at(i));
        }
    }
    return selected;
}