#include "AdvancedSettingsTab.h"
#include "advanced_settings/ConfigurationPage.h"
#include "advanced_settings/VideoSettingsPage.h"
#include "advanced_settings/AudioSettingsPage.h"
#include "advanced_settings/LivestreamSettingsPage.h"
#include "advanced_settings/AuthenticationPage.h"
#include "advanced_settings/OutputTemplatesPage.h"
#include "advanced_settings/DownloadOptionsPage.h"
#include "advanced_settings/MetadataPage.h"
#include "advanced_settings/SubtitlesPage.h"
#include "advanced_settings/BinariesPage.h"
#include "core/ConfigManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QAbstractItemView>
#include <QStackedWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QFrame>
#include <QPalette>
#include <QApplication>
#include <QSizePolicy>
#include <QEvent>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
class SettingsSectionPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsSectionPage(const QList<QWidget *> &pages, QWidget *parent = nullptr)
        : QWidget(parent), m_pages(pages) {
        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);

        QScrollArea *scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        QWidget *scrollWidget = new QWidget(scrollArea);
        QVBoxLayout *contentLayout = new QVBoxLayout(scrollWidget);
        contentLayout->setContentsMargins(0, 0, 8, 0);
        contentLayout->setSpacing(12);

        for (QWidget *page : m_pages) {
            page->setParent(scrollWidget);
            contentLayout->addWidget(page);
        }
        contentLayout->addStretch();

        scrollArea->setWidget(scrollWidget);
        mainLayout->addWidget(scrollArea);
    }

public slots:
    void loadSettings() {
        for (QWidget *page : m_pages) {
            QMetaObject::invokeMethod(page, "loadSettings");
        }
    }

private:
    QList<QWidget *> m_pages;
};
}

AdvancedSettingsTab::AdvancedSettingsTab(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    setupUI();
    loadSettings();
}

AdvancedSettingsTab::~AdvancedSettingsTab() {}

static QString buildCategoryListStyleSheet(const QPalette &palette) {
    const QString baseColor = palette.color(QPalette::Base).name();
    const QString midColor = palette.color(QPalette::Mid).name();
    const QString textColor = palette.color(QPalette::Text).name();
    const QString highlightColor = palette.color(QPalette::Highlight).name();
    const QString highlightedTextColor = palette.color(QPalette::HighlightedText).name();

    return QString(R"(
        QListWidget {
            background: %1;
            border: none;
            border-right: 1px solid %2;
            padding-left: 6px;
            padding-right: 6px;
        }
        QListWidget::item {
            border-radius: 5px;
            padding: 6px 10px;
            margin: 2px 0;
            color: %3;
        }
        QListWidget::item:selected {
            background: %4;
            color: %5;
            font-weight: 600;
        }
    )").arg(baseColor, midColor, textColor, highlightColor, highlightedTextColor);
}

void AdvancedSettingsTab::applyCategoryListStyleSheet() {
    if (!m_categoryList) {
        return;
    }
    // Use the application's global palette because setting a stylesheet on a widget
    // can interfere with its local palette inheritance.
    m_categoryList->setStyleSheet(buildCategoryListStyleSheet(QApplication::palette()));
}

void AdvancedSettingsTab::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *contentLayout = new QHBoxLayout();

    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(170);
    contentLayout->addWidget(m_categoryList);

    m_stackedWidget = new QStackedWidget(this);
    contentLayout->addWidget(m_stackedWidget);

    mainLayout->addLayout(contentLayout, 1);

    m_categoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_categoryList->setUniformItemSizes(true);
    m_categoryList->setSpacing(2);
    applyCategoryListStyleSheet();
    m_categoryList->setToolTip("Switch between advanced setting sections.");
    auto addPage = [this](const QString &title, QWidget *page, const QString &tooltip) {
        auto *item = new QListWidgetItem(title);
        item->setToolTip(tooltip);
        m_categoryList->addItem(item);
        m_stackedWidget->addWidget(page);
    };

    ConfigurationPage* configPage = new ConfigurationPage(m_configManager, this);
    connect(configPage, &ConfigurationPage::themeChanged, this, &AdvancedSettingsTab::onThemeChanged);

    struct PageDescriptor {
        QString title;
        QWidget *page;
        QString tooltip;
    };

    const std::vector<PageDescriptor> descriptors = {
        { "Essentials",
          new SettingsSectionPage({
              configPage,
              new AuthenticationPage(m_configManager, this)
          }, this),
          "Start here: folders, theme, local API, and login cookie access." },
        { "Formats",
          new SettingsSectionPage({
              new VideoSettingsPage(m_configManager, this),
              new AudioSettingsPage(m_configManager, this),
              new LivestreamSettingsPage(m_configManager, this)
          }, this),
          "Default video, audio, and livestream quality/format choices." },
        { "Download Flow",
          new DownloadOptionsPage(m_configManager, this),
          "Downloader engine, automation, clipping, chapters, filenames, and proxy behavior." },
        { "Files & Tags",
          new SettingsSectionPage({
              new OutputTemplatesPage(m_configManager, this),
              new MetadataPage(m_configManager, this),
              new SubtitlesPage(m_configManager, this)
          }, this),
          "Filename templates, metadata, artwork, and subtitles." },
        { "External Tools",
          new BinariesPage(m_configManager, this),
          "Manage paths, versions, installs, and updates for external dependencies." }
    };

    for (const auto &descriptor : descriptors) {
        addPage(descriptor.title, descriptor.page, descriptor.tooltip);
    }

    // Dynamically adjust size policies to prevent hidden tabs from forcing a large minimum window width
    connect(m_categoryList, &QListWidget::currentRowChanged, this, [this](int index) {
        for (int i = 0; i < m_stackedWidget->count(); ++i) {
            QWidget *page = m_stackedWidget->widget(i);
            if (i == index) {
                page->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            } else {
                page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
            }
        }
        m_stackedWidget->setCurrentIndex(index);
    });
    m_categoryList->setCurrentRow(0);

    QHBoxLayout *bottomLayout = new QHBoxLayout();

#ifdef Q_OS_WIN
    bool isDebug = false;
#ifdef QT_DEBUG
    isDebug = true;
#elif !defined(NDEBUG)
    isDebug = true;
#endif
    QCheckBox *consoleToggle = new QCheckBox("Show Debug Console", this);
    consoleToggle->setChecked(m_configManager->get("General", "show_debug_console", isDebug).toBool());
    
    bool ownsConsole = qApp->property("lzy_consoleAllocatedByUs").toBool();
    if (GetConsoleWindow() != NULL && !ownsConsole) {
        consoleToggle->setToolTip("Setting saved for next launch. (Cannot hide the console right now because the app was launched from an existing terminal.)");
    } else {
        consoleToggle->setToolTip("Show or hide the command prompt / debug console window while the application is running.");
    }

    connect(consoleToggle, &QCheckBox::toggled, this, [this](bool checked) {
        m_configManager->set("General", "show_debug_console", checked);
    });
    bottomLayout->addWidget(consoleToggle);
#endif

    bottomLayout->addStretch();

    m_restoreDefaultsButton = new QPushButton("Restore defaults", this);
    m_restoreDefaultsButton->setToolTip("Reset all advanced settings to defaults.");
    bottomLayout->addWidget(m_restoreDefaultsButton);
    mainLayout->addLayout(bottomLayout);
    connect(m_restoreDefaultsButton, &QPushButton::clicked, this, &AdvancedSettingsTab::restoreDefaults);
}

void AdvancedSettingsTab::loadSettings() {
    for (int i = 0; i < m_stackedWidget->count(); ++i) {
        QWidget *page = m_stackedWidget->widget(i);
        // Invoke dynamically so we don't need to manually cast/know the types 
        QMetaObject::invokeMethod(page, "loadSettings");
    }
}

void AdvancedSettingsTab::restoreDefaults() {
    if (QMessageBox::question(this, "Restore Defaults",
        "Are you sure you want to restore all settings to their default values?\nThis cannot be undone.",
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        m_configManager->resetToDefaults();
        loadSettings();
        QMessageBox::information(this, "Defaults Restored", "Settings have been restored to defaults.");
    }
}

void AdvancedSettingsTab::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange) {
        applyCategoryListStyleSheet();
    }
    QWidget::changeEvent(event);
}

void AdvancedSettingsTab::setGalleryDlVersion(const QString &version) {
    if (auto page = m_stackedWidget->findChild<BinariesPage*>()) {
        page->setGalleryDlVersion(version);
    }
}

void AdvancedSettingsTab::setYtDlpVersion(const QString &version) {
    if (auto page = m_stackedWidget->findChild<BinariesPage*>()) {
        page->setYtDlpVersion(version);
    }
}

void AdvancedSettingsTab::navigateToCategory(const QString &categoryTitle) {
    QString targetTitle = categoryTitle;
    if (categoryTitle == "Configuration" || categoryTitle == "Authentication" || categoryTitle == "Authentication Access") {
        targetTitle = "Essentials";
    } else if (categoryTitle == "Video Settings" || categoryTitle == "Audio Settings" || categoryTitle == "Livestream Settings") {
        targetTitle = "Formats";
    } else if (categoryTitle == "Download Options") {
        targetTitle = "Download Flow";
    } else if (categoryTitle == "Output Templates" || categoryTitle == "Output Template" || categoryTitle == "Metadata" || categoryTitle == "Subtitles") {
        targetTitle = "Files & Tags";
    } else if (categoryTitle == "External Binaries" || categoryTitle == "Binaries") {
        targetTitle = "External Tools";
    }

    QList<QListWidgetItem *> items = m_categoryList->findItems(targetTitle, Qt::MatchExactly);
    if (!items.isEmpty()) {
        m_categoryList->setCurrentItem(items.first());
    }
}

void AdvancedSettingsTab::onThemeChanged(const QString &themeName) {
    emit themeChanged(themeName);
}

#include "AdvancedSettingsTab.moc"
