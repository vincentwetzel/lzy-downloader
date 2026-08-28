#include "MissingBinariesDialog.h"

#include "advanced_settings/BinariesPage.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"

#include <QColor>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {
bool isResolved(const ProcessUtils::FoundBinary &foundBinary)
{
    return foundBinary.source != QStringLiteral("Not Found") &&
        foundBinary.source != QStringLiteral("Invalid Custom");
}

QString wrappedPath(const QString &path)
{
    QString displayPath = QFileInfo(path).absoluteFilePath().toHtmlEscaped();
    displayPath.replace(QStringLiteral("\\"), QStringLiteral("\\\u200B"));
    displayPath.replace(QStringLiteral("/"), QStringLiteral("/\u200B"));
    return displayPath;
}
}

MissingBinariesDialog::MissingBinariesDialog(const QStringList &binaryNames,
                                             ConfigManager *configManager,
                                             BinariesPage *binariesPage,
                                             const QHash<QString, QString> &updateDetails,
                                             QWidget *parent)
    : QDialog(parent),
      m_binaryNames(normalizedBinaryList(binaryNames)),
      m_configManager(configManager),
      m_binariesPage(binariesPage),
      m_updateDetails(updateDetails),
      m_summaryLabel(nullptr),
      m_updateAllButton(nullptr),
      m_laterButton(nullptr)
{
    setWindowTitle(tr("Set Up Required Tools"));
    setMinimumWidth(880);
    setModal(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    const QPalette dialogPalette = palette();
    const QColor panelColor = dialogPalette.color(QPalette::Base);
    const QColor borderColor = dialogPalette.color(QPalette::Mid);
    const QColor mutedColor = dialogPalette.color(QPalette::PlaceholderText);
    const QColor accentColor = dialogPalette.color(QPalette::Highlight);
    const QColor accentTextColor = dialogPalette.color(QPalette::HighlightedText);
    const QColor accentHoverColor = accentColor.lighter(115);
    setStyleSheet(QStringLiteral(
        "QGroupBox { background: %1; border: 1px solid %2; border-radius: 6px; "
        "margin-top: 12px; padding-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; "
        "font-weight: 600; }"
        "QLabel#setupIntro, QLabel#setupSummary { color: %3; }"
        "QLabel#columnHeader { color: %3; font-weight: 600; }"
        "QPushButton#primaryActionButton { background: %4; color: %5; border: 0; "
        "border-radius: 4px; padding: 6px 12px; font-weight: 600; }"
        "QPushButton#primaryActionButton:hover { background: %6; }"
        "QPushButton#primaryActionButton:disabled { background: %2; color: %3; }"
        "QPushButton#secondaryActionButton { padding: 6px 14px; }")
        .arg(panelColor.name(), borderColor.name(), mutedColor.name(), accentColor.name(),
             accentTextColor.name(), accentHoverColor.name()));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 18, 20, 16);
    mainLayout->setSpacing(12);
    auto *introLabel = new QLabel(
        tr("LzyDownloader needs attention for the tools below. Missing tools will be installed "
           "using the displayed recommended method; detected tools are updated in their existing locations."),
        this);
    introLabel->setObjectName(QStringLiteral("setupIntro"));
    introLabel->setWordWrap(true);
    introLabel->setMinimumWidth(0);
    introLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    mainLayout->addWidget(introLabel);

    auto *toolsGroup = new QGroupBox(tr("Tools needing attention"), this);
    toolsGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *grid = new QGridLayout(toolsGroup);
    grid->setContentsMargins(14, 18, 14, 14);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(8);
    grid->setColumnMinimumWidth(0, 80);
    grid->setColumnMinimumWidth(2, 340);
    grid->setColumnStretch(1, 1);

    auto *toolHeader = new QLabel(tr("Tool"), toolsGroup);
    auto *statusHeader = new QLabel(tr("Status"), toolsGroup);
    auto *actionHeader = new QLabel(tr("Action"), toolsGroup);
    toolHeader->setObjectName(QStringLiteral("columnHeader"));
    statusHeader->setObjectName(QStringLiteral("columnHeader"));
    actionHeader->setObjectName(QStringLiteral("columnHeader"));
    grid->addWidget(toolHeader, 0, 0);
    grid->addWidget(statusHeader, 0, 1);
    grid->addWidget(actionHeader, 0, 2);

    int row = 1;
    for (const QString &binaryName : m_binaryNames) {
        auto *nameLabel = new QLabel(binaryName, toolsGroup);
        auto *statusLabel = new QLabel(tr("Checking..."), toolsGroup);
        statusLabel->setWordWrap(true);
        statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusLabel->setMinimumWidth(0);
        statusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        statusLabel->setTextFormat(Qt::RichText);

        auto *actionButton = new QPushButton(toolsGroup);
        actionButton->setObjectName(QStringLiteral("primaryActionButton"));
        actionButton->setMinimumWidth(260);
        actionButton->setMinimumHeight(30);
        actionButton->setEnabled(m_binariesPage != nullptr);
        auto *browseButton = new QPushButton(tr("Browse..."), toolsGroup);
        browseButton->setObjectName(QStringLiteral("secondaryActionButton"));
        browseButton->setMinimumHeight(30);
        browseButton->setToolTip(tr("Select an existing %1 executable from disk.").arg(binaryName));
        browseButton->setEnabled(m_binariesPage != nullptr);

        auto *actionsLayout = new QHBoxLayout();
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->addWidget(actionButton);
        actionsLayout->addWidget(browseButton);
        auto *actionsWidget = new QWidget(toolsGroup);
        actionsWidget->setLayout(actionsLayout);

        grid->addWidget(nameLabel, row, 0);
        grid->addWidget(statusLabel, row, 1);
        grid->addWidget(actionsWidget, row, 2);
        m_rows.insert(binaryName, {statusLabel, actionButton, browseButton});

        connect(actionButton, &QPushButton::clicked, this, [this, binaryName]() {
            if (!m_binariesPage) {
                return;
            }
            const ProcessUtils::FoundBinary foundBinary = ProcessUtils::resolveBinary(binaryName, m_configManager);
            if (!isResolved(foundBinary)) {
                m_binariesPage->installRecommendedBinary(binaryName);
            } else {
                m_binariesPage->updateBinaryFor(binaryName,
                    !m_binariesPage->canUpdateBinaryAutomatically(binaryName));
            }
            refreshStatuses();
        });
        connect(browseButton, &QPushButton::clicked, this, [this, binaryName]() {
            if (m_binariesPage) {
                m_binariesPage->browseBinaryFor(binaryName);
                refreshStatuses();
            }
        });
        ++row;
    }
    mainLayout->addWidget(toolsGroup);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("setupSummary"));
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setMinimumWidth(0);
    m_summaryLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    mainLayout->addWidget(m_summaryLabel);

    auto *buttonsLayout = new QHBoxLayout();
    m_laterButton = new QPushButton(tr("Later"), this);
    m_laterButton->setObjectName(QStringLiteral("secondaryActionButton"));
    m_updateAllButton = new QPushButton(tr("Update All"), this);
    m_updateAllButton->setObjectName(QStringLiteral("primaryActionButton"));
    m_laterButton->setToolTip(tr("Close setup for now."));
    m_updateAllButton->setToolTip(tr("Install missing tools and update detected tools that can be updated automatically."));
    buttonsLayout->addWidget(m_laterButton);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(m_updateAllButton);
    mainLayout->addLayout(buttonsLayout);

    connect(m_laterButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_updateAllButton, &QPushButton::clicked, this, [this]() {
        if (allBinariesResolved()) {
            accept();
            return;
        }
        runUpdateAll();
    });

    refreshStatuses();
    adjustSize();
    resize(sizeHint());
}

bool MissingBinariesDialog::allBinariesResolved() const
{
    for (const QString &binaryName : m_binaryNames) {
        if (needsAttention(binaryName)) {
            return false;
        }
    }
    return true;
}

void MissingBinariesDialog::refreshStatuses()
{
    ProcessUtils::clearCache();
    int missingCount = 0;
    int updateCount = 0;
    int manualUpdateCount = 0;
    const QString readyColor = palette().color(QPalette::Link).name();
    const QString attentionColor = palette().color(QPalette::Highlight).name();

    for (const QString &binaryName : m_binaryNames) {
        const BinaryRow row = m_rows.value(binaryName);
        if (!row.statusLabel) {
            continue;
        }

        const ProcessUtils::FoundBinary foundBinary = ProcessUtils::resolveBinary(binaryName, m_configManager);
        const bool resolved = isResolved(foundBinary);
        const bool updateAvailable = resolved && m_configManager->get(
            QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), false).toBool();

        if (!resolved) {
            ++missingCount;
            const bool invalidPath = foundBinary.source == QStringLiteral("Invalid Custom");
            row.statusLabel->setText(invalidPath
                ? tr("<b><span style='color:%1'>Invalid manual path</span></b><br>%2")
                    .arg(attentionColor, wrappedPath(foundBinary.path))
                : tr("<b><span style='color:%1'>Missing</span></b> — a new app-managed copy will be installed.")
                    .arg(attentionColor));
            if (row.actionButton) {
                const QString action = m_binariesPage
                    ? tr("Install: %1").arg(m_binariesPage->recommendedInstallLabel(binaryName))
                    : tr("Install unavailable");
                row.actionButton->setText(action);
                row.actionButton->setToolTip(tr("Install a new copy of %1 using the displayed method.").arg(binaryName));
                row.actionButton->setVisible(true);
            }
        } else if (updateAvailable) {
            ++updateCount;
            const bool canUpdate = m_binariesPage && m_binariesPage->canUpdateBinaryAutomatically(binaryName);
            if (!canUpdate) {
                ++manualUpdateCount;
            }
            const QString latestVersion = m_configManager->get(
                QStringLiteral("Binaries"), QStringLiteral("%1_latest_version").arg(binaryName)).toString();
            const QString details = m_updateDetails.value(binaryName);
            const QString updateText = latestVersion.isEmpty()
                ? tr("Update available")
                : tr("Update available (%1)").arg(latestVersion.toHtmlEscaped());
            row.statusLabel->setText(tr("<b><span style='color:%1'>%2</span></b><br>"
                                        "<b>Existing binary:</b> %3<br>%4%5")
                .arg(attentionColor, updateText, foundBinary.source.toHtmlEscaped(), wrappedPath(foundBinary.path),
                     details.isEmpty() ? QString() : tr("<br>%1").arg(details.toHtmlEscaped())));
            if (row.actionButton) {
                row.actionButton->setText(canUpdate ? tr("Update existing") : tr("Manual update..."));
                row.actionButton->setToolTip(canUpdate
                    ? tr("Update the detected %1 in its existing location.").arg(binaryName)
                    : tr("Show manual update options for the detected %1.").arg(binaryName));
                row.actionButton->setVisible(true);
            }
        } else {
            row.statusLabel->setText(tr("<b><span style='color:%1'>Ready</span></b><br>"
                                        "<b>Existing binary:</b> %2<br>%3")
                .arg(readyColor, foundBinary.source.toHtmlEscaped(), wrappedPath(foundBinary.path)));
            if (row.actionButton) {
                row.actionButton->setVisible(false);
            }
        }

        if (row.browseButton) {
            row.browseButton->setVisible(m_binariesPage != nullptr);
        }
        if (m_binariesPage) {
            m_binariesPage->refreshBinaryStatus(binaryName);
        }
    }

    const bool complete = allBinariesResolved();
    if (m_updateAllButton) {
        m_updateAllButton->setText(complete ? tr("Done") : tr("Update All"));
        m_updateAllButton->setEnabled(m_binariesPage != nullptr || complete);
    }
    if (m_summaryLabel) {
        if (complete) {
            m_summaryLabel->setText(tr("All listed tools are ready."));
        } else {
            m_summaryLabel->setText(tr("%1 new installation(s) and %2 existing-binary update(s) need attention.%3")
                .arg(missingCount).arg(updateCount)
                .arg(manualUpdateCount == 0 ? QString() : tr(" %1 update(s) require a manual choice.").arg(manualUpdateCount)));
        }
    }
}

void MissingBinariesDialog::runUpdateAll()
{
    if (!m_binariesPage) {
        return;
    }

    QStringList operations;
    for (const QString &binaryName : m_binaryNames) {
        const ProcessUtils::FoundBinary foundBinary = ProcessUtils::resolveBinary(binaryName, m_configManager);
        if (!isResolved(foundBinary)) {
            operations << binaryName;
        } else if (m_configManager->get(QStringLiteral("Binaries"),
                   QStringLiteral("%1_update_available").arg(binaryName), false).toBool() &&
                   m_binariesPage->canUpdateBinaryAutomatically(binaryName)) {
            operations << binaryName;
        }
    }

    if (operations.contains(QStringLiteral("ffmpeg"))) {
        operations.removeAll(QStringLiteral("ffprobe"));
    }
    if (operations.isEmpty()) {
        if (m_summaryLabel) {
            m_summaryLabel->setText(tr("The remaining updates need a manual choice. Use the action beside each tool."));
        }
        return;
    }

    m_updateAllButton->setEnabled(false);
    m_laterButton->setEnabled(false);
    for (const QString &binaryName : operations) {
        const ProcessUtils::FoundBinary foundBinary = ProcessUtils::resolveBinary(binaryName, m_configManager);
        if (isResolved(foundBinary)) {
            m_binariesPage->updateBinaryFor(binaryName, false, true);
        } else {
            m_binariesPage->installRecommendedBinary(binaryName, true);
        }
    }
    m_laterButton->setEnabled(true);
    refreshStatuses();
}

bool MissingBinariesDialog::needsAttention(const QString &binaryName) const
{
    const ProcessUtils::FoundBinary foundBinary = ProcessUtils::resolveBinary(binaryName, m_configManager);
    return !isResolved(foundBinary) || m_configManager->get(
        QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), false).toBool();
}

QStringList MissingBinariesDialog::unresolvedBinaries() const
{
    QStringList unresolved;
    for (const QString &binaryName : m_binaryNames) {
        const ProcessUtils::FoundBinary foundBinary = ProcessUtils::resolveBinary(binaryName, m_configManager);
        if (!isResolved(foundBinary)) {
            unresolved << binaryName;
        }
    }
    return unresolved;
}

QStringList MissingBinariesDialog::normalizedBinaryList(const QStringList &binaryNames)
{
    QStringList normalized;
    QSet<QString> seen;
    for (const QString &binaryName : binaryNames) {
        const QString trimmed = binaryName.trimmed();
        if (!trimmed.isEmpty() && !seen.contains(trimmed)) {
            normalized << trimmed;
            seen.insert(trimmed);
        }
    }
    return normalized;
}
