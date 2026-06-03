#include "MainWindowUiBuilder.h"
#include "core/ConfigManager.h"
#include "StartTab.h"
#include "ActiveDownloadsTab.h"
#include "AdvancedSettingsTab.h"
#include "SortingTab.h"
#include "ToggleSwitch.h"

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QApplication>
#include <QStyleHints>
#include <QSizePolicy>
#include <QMessageBox> // For QMessageBox

const QString GITHUB_PROJECT_URL = QStringLiteral("https://github.com/vincentwetzel/LzyDownloader");
const QString DEVELOPER_DISCORD_URL_PART1 = QStringLiteral("https://discord.gg/");
const QString DEVELOPER_DISCORD_URL_PART2 = QStringLiteral("NfWaqK");
const QString DEVELOPER_DISCORD_URL_PART3 = QStringLiteral("gYRG");

MainWindowUiBuilder::MainWindowUiBuilder(ConfigManager *configManager, QObject *parent)
    : QObject(parent),
      m_configManager(configManager),
      m_tabWidget(nullptr),
      m_speedLabel(nullptr),
      m_queuedDownloadsLabel(nullptr),
      m_activeDownloadsLabel(nullptr),
      m_completedDownloadsLabel(nullptr),
      m_errorDownloadsLabel(nullptr),
      m_exitAfterSwitch(nullptr)
{
}

void MainWindowUiBuilder::build(QMainWindow *mainWindow, QVBoxLayout *mainLayout, StartTab *startTab, ActiveDownloadsTab *activeDownloadsTab, AdvancedSettingsTab *advancedSettingsTab, SortingTab *sortingTab)
{
    mainWindow->setMinimumWidth(850);
    mainWindow->resize(850, 600);

    m_tabWidget = new QTabWidget(mainWindow);
    mainLayout->addWidget(m_tabWidget);

    QComboBox *languageCombo = new QComboBox(mainWindow);
    languageCombo->setToolTip(tr("Select Application Language"));
    QStringList languagesWithFlags = {
        QStringLiteral("🇺🇸 English"), QStringLiteral("🇪🇸 Spanish"), QStringLiteral("🇵🇹 Portuguese"), QStringLiteral("🇷🇺 Russian"), QStringLiteral("🇩🇪 German"), QStringLiteral("🇫🇷 French"),
        QStringLiteral("🇮🇹 Italian"), QStringLiteral("🇨🇳 Mandarin"), QStringLiteral("🇮🇳 Hindi"), QStringLiteral("🇧🇩 Bengali"), QStringLiteral("🇯🇵 Japanese"), QStringLiteral("🇵🇰 Western Punjabi"),
        QStringLiteral("🇹🇷 Turkish"), QStringLiteral("🇻🇳 Vietnamese"), QStringLiteral("🇭🇰 Yue Chinese"), QStringLiteral("🇪🇬 Egyptian Arabic"), QStringLiteral("🇨🇳 Wu Chinese"),
        QStringLiteral("🇮🇳 Marathi"), QStringLiteral("🇮🇳 Telugu"), QStringLiteral("🇰🇷 Korean"), QStringLiteral("🇮🇳 Tamil"), QStringLiteral("🇵🇰 Urdu"), QStringLiteral("🇮🇩 Indonesian"),
        QStringLiteral("🇮🇩 Javanese"), QStringLiteral("🇮🇷 Iranian Persian"), QStringLiteral("🇳🇬 Hausa"), QStringLiteral("🇮🇳 Gujarati"), QStringLiteral("🇱🇧 Levantine Arabic"),
        QStringLiteral("🇮🇳 Bhojpuri")
    };
    languageCombo->addItems(languagesWithFlags);

    QString savedLang = m_configManager->get(QStringLiteral("General"), QStringLiteral("language"), QStringLiteral("🇺🇸 English")).toString();
    int index = languageCombo->findText(savedLang);
    if (index >= 0) languageCombo->setCurrentIndex(index);
    m_tabWidget->setCornerWidget(languageCombo, Qt::TopRightCorner);
    connect(languageCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (m_configManager->get(QStringLiteral("General"), QStringLiteral("language"), QStringLiteral("🇺🇸 English")).toString() != text) {
            m_configManager->set(QStringLiteral("General"), QStringLiteral("language"), text);
            QMessageBox::information(qobject_cast<QWidget*>(parent()), tr("Language Changed"), tr("Language changed to %1.\n\nPlease restart the application for changes to take full effect.").arg(text));
        }
    });

    m_tabWidget->addTab(startTab, tr("Start Download"));
    m_tabWidget->addTab(activeDownloadsTab, tr("Active Downloads"));
    m_tabWidget->addTab(sortingTab, tr("Sorting Rules"));
    m_tabWidget->addTab(advancedSettingsTab, tr("Advanced Settings"));

    // Dynamically adjust size policies to prevent hidden tabs from forcing a large minimum window width.
    // This ensures that only the currently visible tab influences the window's minimum size.
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            if (QWidget *widget = m_tabWidget->widget(i)) {
                widget->setSizePolicy(i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored, QSizePolicy::Preferred);
            }
        }
        m_tabWidget->currentWidget()->updateGeometry();
    });

    // Set initial state for all but the first tab
    for (int i = 1; i < m_tabWidget->count(); ++i) {
        if (QWidget *widget = m_tabWidget->widget(i)) {
            widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        }
    }

    QVBoxLayout *footerContainer = new QVBoxLayout();
    QHBoxLayout *footerTopRow = new QHBoxLayout();
    QLabel *githubLink = new QLabel(tr("<a href=\"%1\">Source Code</a>").arg(GITHUB_PROJECT_URL));
    githubLink->setOpenExternalLinks(true);
    githubLink->setToolTip(tr("Visit the project's source code repository on GitHub."));

    QLabel *discordLink = new QLabel(tr("<a href=\"%1%2%3\"><img src=\":/ui/assets/discord.png\" alt=\"Developer Discord\" width=\"24\" height=\"24\"></a>")
                                         .arg(DEVELOPER_DISCORD_URL_PART1)
                                         .arg(DEVELOPER_DISCORD_URL_PART2)
                                         .arg(DEVELOPER_DISCORD_URL_PART3));
    discordLink->setOpenExternalLinks(true);
    discordLink->setToolTip(tr("Join the developer Discord server."));

    m_queuedDownloadsLabel = new QLabel(tr("Queued: 0"), mainWindow);
    m_queuedDownloadsLabel->setToolTip(tr("Number of downloads waiting to start."));
    m_activeDownloadsLabel = new QLabel(tr("Active: 0"), mainWindow);
    m_activeDownloadsLabel->setToolTip(tr("Number of currently active downloads."));
    m_completedDownloadsLabel = new QLabel(tr("Completed: 0"), mainWindow);
    m_completedDownloadsLabel->setToolTip(tr("Number of successfully completed downloads."));
    m_errorDownloadsLabel = new QLabel(tr("Errors: 0"), mainWindow);
    m_errorDownloadsLabel->setToolTip(tr("Number of downloads that failed with an error."));
    m_speedLabel = new QLabel(tr("Current Speed: 0.00 MB/s"), mainWindow);
    m_speedLabel->setToolTip(tr("Total download speed across all active transfers."));

    QLabel *exitAfterLabel = new QLabel(tr("Exit after all downloads complete:"), mainWindow);
    m_exitAfterSwitch = new ToggleSwitch(mainWindow);
    m_exitAfterSwitch->setToolTip(tr("If switched on, the application will automatically close once all your downloads are finished."));
    m_exitAfterSwitch->setChecked(m_configManager->get(QStringLiteral("General"), QStringLiteral("exit_after"), false).toBool());

    footerTopRow->addWidget(githubLink);
    footerTopRow->addSpacing(20);
    footerTopRow->addWidget(discordLink);
    footerTopRow->addStretch();
    footerTopRow->addWidget(exitAfterLabel);
    footerTopRow->addWidget(m_exitAfterSwitch);

    QHBoxLayout *footerBottomRow = new QHBoxLayout();
    footerBottomRow->addStretch();
    footerBottomRow->addWidget(m_queuedDownloadsLabel);
    footerBottomRow->addSpacing(10);
    footerBottomRow->addWidget(m_activeDownloadsLabel);
    footerBottomRow->addSpacing(10);
    footerBottomRow->addWidget(m_completedDownloadsLabel);
    footerBottomRow->addSpacing(10);
    footerBottomRow->addWidget(m_errorDownloadsLabel);
    footerBottomRow->addSpacing(20);
    footerBottomRow->addWidget(m_speedLabel);

    footerContainer->addLayout(footerTopRow);
    footerContainer->addLayout(footerBottomRow);
    mainLayout->addLayout(footerContainer);
}