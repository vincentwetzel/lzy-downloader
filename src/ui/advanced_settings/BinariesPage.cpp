#include "BinariesPage.h"

#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include "core/SmartBinaryResolver.h"
#include "core/BaseBinaryUpdater.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QTextCursor>
#include <QTimer>
#include <QPointer>
#include <QEvent>
#include <QSettings>

namespace {
QString getWindowsAppsDir() {
    const QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
    if (localAppData.isEmpty()) {
        return QString();
    }
    return QDir(localAppData).filePath(QStringLiteral("Microsoft/WindowsApps"));
}
}

BinariesPage::BinariesPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QWidget *scrollWidget = new QWidget(scrollArea);
    scrollWidget->setMinimumWidth(0);
    scrollWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    QVBoxLayout *layout = new QVBoxLayout(scrollWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QGroupBox *group = new QGroupBox(tr("External Binaries"), scrollWidget);
    group->setToolTip(tr("Review detected tool locations, choose manual executable paths, install missing tools, or update installed tools."));
    group->setMinimumWidth(0);
    group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    QVBoxLayout *groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(6);
    groupLayout->setContentsMargins(12, 10, 12, 10);

    QLabel *introLabel = new QLabel(
        tr("<b>REQUIRED:</b> yt-dlp, ffmpeg, ffprobe, deno<br>"
           "<b>OPTIONAL:</b> gallery-dl, aria2c<br><br>"
           "<b>Browse</b> sets a manual path override. <b>Clear Path</b> reverts to auto-detection.<br>"
           "<b>Install</b> opens package-manager or official website download options.<br><br>"
           "<span style='color: #d97706;'><b>Note:</b> If a package-manager install does not appear after Refresh, "
           "restart LzyDownloader or use Browse to select the installed executable.</span>"),
        group
    );

    introLabel->setWordWrap(true);
    introLabel->setTextFormat(Qt::RichText);
    introLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    introLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    introLabel->setToolTip(tr("Explains which external tools are required, which are optional, and how Browse, Clear Path, Install, and Refresh affect detection."));

    groupLayout->addWidget(introLabel);

    QPushButton *refreshButton = new QPushButton(tr("Refresh All Statuses"), group);
    refreshButton->setObjectName(QStringLiteral("refreshAllStatusesButton"));
    refreshButton->setToolTip(tr("Re-scan configured paths and auto-detected binaries.\nIf a newly installed package manager tool is still missing, restart the app to refresh the system PATH."));
    refreshButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    connect(refreshButton, &QPushButton::clicked, this, [this, refreshButton]() {
        refreshButton->setEnabled(false);
        m_binaryWarnings.clear();

        for (auto it = m_statusLabels.constBegin(); it != m_statusLabels.constEnd(); ++it) {
            it.value()->setText(tr("<b>Status:</b> Refreshing..."));
        }

        for (auto it = m_installButtons.constBegin(); it != m_installButtons.constEnd(); ++it) {
            it.value()->setEnabled(false);
        }

        for (auto it = m_updateButtons.constBegin(); it != m_updateButtons.constEnd(); ++it) {
            it.value()->setEnabled(false);
        }

        for (const QString &binaryName : m_configKeys.keys()) {
            if (QPushButton *clearBtn = this->findChild<QPushButton *>(QStringLiteral("%1_clearButton").arg(binaryName))) {
                clearBtn->setEnabled(false);
            }
        }

        qInfo() << "[BinariesPage] 'Refresh All Statuses' button clicked! Clearing warnings and ProcessUtils cache...";
        ProcessUtils::clearCache();

        constexpr int REFRESH_DELAY_MS = 150;
        QTimer::singleShot(REFRESH_DELAY_MS, this, [this, refreshButton]() {
            qInfo() << "[BinariesPage] Refresh delay completed. Re-loading settings...";
            loadSettings();
            refreshButton->setEnabled(true);
        });
    });

    groupLayout->addWidget(refreshButton, 0, Qt::AlignRight);

    setupRow(groupLayout, QStringLiteral("yt-dlp"), QStringLiteral("yt-dlp"), QStringLiteral("yt-dlp_path"), QStringLiteral("https://github.com/yt-dlp/yt-dlp-nightly-builds/releases/latest"), false);
    setupRow(groupLayout, QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg_path"), QStringLiteral("https://ffmpeg.org/download.html"), false);
    setupRow(groupLayout, QStringLiteral("ffprobe"), QStringLiteral("ffprobe"), QStringLiteral("ffprobe_path"), QStringLiteral("https://ffmpeg.org/download.html"), false);
    setupRow(groupLayout, QStringLiteral("deno"), QStringLiteral("deno"), QStringLiteral("deno_path"), QStringLiteral("https://deno.com/"), false);
    setupRow(groupLayout, QStringLiteral("gallery-dl"), QStringLiteral("gallery-dl"), QStringLiteral("gallery-dl_path"), QStringLiteral("https://github.com/mikf/gallery-dl"), true);
    setupRow(groupLayout, QStringLiteral("aria2c"), QStringLiteral("aria2c"), QStringLiteral("aria2c_path"), QStringLiteral("https://github.com/aria2/aria2/releases"), true);

    layout->addWidget(group, 0, Qt::AlignTop);

    scrollArea->setWidget(scrollWidget);

    mainLayout->addWidget(scrollArea);

    connect(m_configManager, &ConfigManager::settingChanged, this, &BinariesPage::handleConfigSettingChanged);
}

bool BinariesPage::eventFilter(QObject *watched, QEvent *event) {
    return QWidget::eventFilter(watched, event);
}


void BinariesPage::setupRow(QVBoxLayout *layout,
                            const QString &binaryName,
                            const QString &labelText,
                            const QString &configKey,
                            const QString &manualUrl,
                            bool optional) {
    m_configKeys.insert(binaryName, configKey);
    m_manualUrls.insert(binaryName, manualUrl);
    m_displayNames.insert(binaryName, labelText);

    if (optional) {
        m_optionalBinaries.insert(binaryName);
    }

    QGroupBox *rowGroup = new QGroupBox(layout->parentWidget());
    rowGroup->setMinimumWidth(0);
    rowGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    const QString title = optional ? tr("%1 (Optional)").arg(labelText) : labelText;
    rowGroup->setTitle(title);

    QFont groupFont = rowGroup->font();
    groupFont.setBold(true);
    rowGroup->setFont(groupFont);

    QHBoxLayout *rowLayout = new QHBoxLayout(rowGroup);
    rowLayout->setSpacing(12);
    rowLayout->setContentsMargins(10, 6, 10, 6);
    rowLayout->setAlignment(Qt::AlignTop);

    QLabel *statusLabel = new QLabel(rowGroup);
    statusLabel->setWordWrap(false);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLabel->setToolTip(tr("Shows whether %1 was found automatically, found through a manual override, or is missing.").arg(labelText));
    statusLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    statusLabel->setMinimumWidth(0);
    statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

    QLabel *versionLabel = new QLabel(tr("Version: Unknown"), rowGroup);
    versionLabel->setWordWrap(false);
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    versionLabel->setToolTip(tr("Detected version reported by %1. Shows Unknown until a version check succeeds.").arg(labelText));
    versionLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    versionLabel->setMinimumWidth(0);
    versionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

    QString description;
    if (binaryName == QStringLiteral("yt-dlp")) {
        description = tr("Downloads video and audio from online platforms.");
    } else if (binaryName == QStringLiteral("ffmpeg")) {
        description = tr("Merges and converts media formats.");
    } else if (binaryName == QStringLiteral("ffprobe")) {
        description = tr("Analyzes media files and metadata.");
    } else if (binaryName == QStringLiteral("deno")) {
        description = tr("Executes JavaScript for solving anti-bot challenges.");
    } else if (binaryName == QStringLiteral("gallery-dl")) {
        description = tr("Downloads image galleries.");
    } else if (binaryName == QStringLiteral("aria2c")) {
        description = tr("Accelerates downloads using multiple connections.");
    }

    QLabel *descLabel = new QLabel(QStringLiteral("<i>%1</i>").arg(description), rowGroup);
    descLabel->setToolTip(tr("What LzyDownloader uses %1 for.").arg(labelText));
    descLabel->setWordWrap(false);
    descLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    descLabel->setMinimumWidth(0);
    descLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

    QVBoxLayout *leftCol = new QVBoxLayout();
    leftCol->setSpacing(2);
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->setAlignment(Qt::AlignTop);
    leftCol->addWidget(descLabel);
    leftCol->addWidget(statusLabel);
    leftCol->addWidget(versionLabel);

    QPushButton *browseButton = new QPushButton(tr("Browse..."), rowGroup);
    QPushButton *clearButton = new QPushButton(tr("Clear Path"), rowGroup);
    clearButton->setObjectName(QStringLiteral("%1_clearButton").arg(binaryName));

    QPushButton *installButton = new QPushButton(tr("Install..."), rowGroup);
    QPushButton *updateButton = new QPushButton(tr("Update"), rowGroup);

    browseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    clearButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    installButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    updateButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    if (binaryName == QStringLiteral("yt-dlp")) {
        updateButton->setObjectName(QStringLiteral("updateYtDlpButton"));
        updateButton->setToolTip(tr("Check for and install yt-dlp updates."));
    } else if (binaryName == QStringLiteral("gallery-dl")) {
        updateButton->setObjectName(QStringLiteral("updateGalleryDlButton"));
        updateButton->setToolTip(tr("Check for and install gallery-dl updates."));
    } else {
        updateButton->setToolTip(tr("Update %1.").arg(labelText));
    }

    QFont childFont = browseButton->font();
    childFont.setBold(false);

    browseButton->setFont(childFont);
    clearButton->setFont(childFont);
    installButton->setFont(childFont);
    updateButton->setFont(childFont);
    statusLabel->setFont(childFont);
    versionLabel->setFont(childFont);
    descLabel->setFont(childFont);

    browseButton->setToolTip(tr("Choose a specific %1 executable from disk to set a manual override.").arg(labelText));
    clearButton->setToolTip(tr("Remove the saved manual path for %1 and return to automatic detection.").arg(labelText));
    installButton->setToolTip(tr("Open detected package-manager options or the official download page for %1.").arg(labelText));

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setAlignment(Qt::AlignTop | Qt::AlignRight);
    buttonLayout->addWidget(updateButton);
    buttonLayout->addWidget(installButton);
    buttonLayout->addWidget(browseButton);
    buttonLayout->addWidget(clearButton);

    QVBoxLayout *rightCol = new QVBoxLayout();
    rightCol->setSpacing(0);
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->setAlignment(Qt::AlignTop | Qt::AlignRight);
    rightCol->addLayout(buttonLayout);

    rowLayout->addLayout(leftCol, 1);
    rowLayout->addLayout(rightCol, 0);

    layout->addWidget(rowGroup);

    m_statusLabels.insert(binaryName, statusLabel);
    m_versionLabels.insert(binaryName, versionLabel);
    m_installButtons.insert(binaryName, installButton);
    m_updateButtons.insert(binaryName, updateButton);

    connect(browseButton, &QPushButton::clicked, this, [this, binaryName]() {
        browseBinaryFor(binaryName);
    });

    connect(clearButton, &QPushButton::clicked, this, [this, binaryName]() {
        saveBinaryOverride(binaryName, QString());
    });

    connect(installButton, &QPushButton::clicked, this, [this, binaryName]() {
        installBinaryFor(binaryName);
    });

    connect(updateButton, &QPushButton::clicked, this, [this, binaryName]() {
        const ProcessUtils::FoundBinary foundBinary = SmartBinaryResolver::resolve(binaryName, m_configManager);
        QString pathLower = foundBinary.path.toLower();

        QString manager;
        QString updateProgram;
        QStringList updateArgs;

        if (pathLower.contains(QStringLiteral("scoop"))) {
            manager = QStringLiteral("Scoop");
            updateProgram = QStringLiteral("scoop");

            if (binaryName == QStringLiteral("yt-dlp")) {
                updateArgs = {QStringLiteral("update"), QStringLiteral("yt-dlp-nightly")};
            } else if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
                updateArgs = {QStringLiteral("update"), QStringLiteral("ffmpeg")};
            } else {
                updateArgs = {QStringLiteral("update"), binaryName};
            }

        } else if (pathLower.contains(QStringLiteral("windowsapps"))) {
            manager = QStringLiteral("WinGet");
            updateProgram = QStringLiteral("winget");

            if (binaryName == QStringLiteral("gallery-dl")) {
                updateArgs = {QStringLiteral("upgrade"), QStringLiteral("--id"), QStringLiteral("mikf.gallery-dl"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")};
            } else if (binaryName == QStringLiteral("yt-dlp")) {
                updateArgs = {QStringLiteral("upgrade"), QStringLiteral("--id"), QStringLiteral("yt-dlp.yt-dlp"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")};
            } else if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
                updateArgs = {QStringLiteral("upgrade"), QStringLiteral("--id"), QStringLiteral("Gyan.FFmpeg"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")};
            } else if (binaryName == QStringLiteral("aria2c")) {
                updateArgs = {QStringLiteral("upgrade"), QStringLiteral("--id"), QStringLiteral("aria2.aria2"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")};
            } else if (binaryName == QStringLiteral("deno")) {
                updateArgs = {QStringLiteral("upgrade"), QStringLiteral("--id"), QStringLiteral("DenoLand.Deno"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")};
            } else {
                updateArgs = {QStringLiteral("upgrade"), binaryName, QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")};
            }

        } else if (pathLower.contains(QStringLiteral("python")) ||
                   pathLower.contains(QStringLiteral("pip")) ||
                   pathLower.contains(QStringLiteral("scripts")) ||
                   pathLower.contains(QStringLiteral("site-packages"))) {
            manager = QStringLiteral("pip");
            updateProgram = QStringLiteral("pip");

            if (binaryName == QStringLiteral("yt-dlp")) {
                updateArgs = {QStringLiteral("install"), QStringLiteral("-U"), QStringLiteral("--pre"), QStringLiteral("yt-dlp")};
            } else {
                updateArgs = {QStringLiteral("install"), QStringLiteral("-U"), binaryName};
            }

        } else if (pathLower.contains(QStringLiteral("homebrew")) ||
                   pathLower.contains(QStringLiteral("cellar")) ||
                   pathLower.contains(QStringLiteral("linuxbrew"))) {
            manager = QStringLiteral("Homebrew");
            updateProgram = QStringLiteral("brew");

            if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
                updateArgs = {QStringLiteral("upgrade"), QStringLiteral("ffmpeg")};
            } else {
                updateArgs = {QStringLiteral("upgrade"), binaryName};
            }

        } else if (pathLower.contains(QStringLiteral("chocolatey")) ||
                   pathLower.contains(QStringLiteral("choco"))) {
            manager = QStringLiteral("Chocolatey");
            updateProgram = QStringLiteral("choco");

            if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
                updateArgs = {QStringLiteral("upgrade"), QStringLiteral("-y"), QStringLiteral("ffmpeg")};
            } else {
                updateArgs = {QStringLiteral("upgrade"), QStringLiteral("-y"), binaryName};
            }
        }

        const bool isStandalone = manager.isEmpty();

        if (isStandalone) {
            if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe") || binaryName == QStringLiteral("aria2c")) {
#ifdef Q_OS_WIN
                if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
                    QMessageBox msgBox(this);
                    msgBox.setWindowTitle(tr("Update %1").arg(displayName(binaryName)));
                    msgBox.setTextFormat(Qt::RichText);
                    msgBox.setText(tr("A recommended update is available for <b>%1</b>.<br><br>"
                                     "How would you like to update?<br><br>"
                                     "<b>Automated Update (Recommended):</b> Downloads and installs the latest stable build of FFmpeg and FFprobe directly to your local application folder.<br>"
                                     "<b>Manual Update:</b> Opens the official download page in your web browser.")
                                      .arg(displayName(binaryName)));
                    msgBox.setIcon(QMessageBox::Question);
                    QPushButton *autoBtn = msgBox.addButton(tr("Automated Update"), QMessageBox::AcceptRole);
                    QPushButton *manualBtn = msgBox.addButton(tr("Manual Update"), QMessageBox::ActionRole);
                    msgBox.addButton(QMessageBox::Cancel);
                    msgBox.exec();

                    if (msgBox.clickedButton() == autoBtn) {
                        installBinaryFor(binaryName);
                        return;
                    } else if (msgBox.clickedButton() == manualBtn) {
                        QDesktopServices::openUrl(QUrl(m_manualUrls.value(binaryName)));
                        return;
                    } else {
                        return;
                    }
                }
#endif
                QDesktopServices::openUrl(QUrl(m_manualUrls.value(binaryName)));
                QMessageBox::information(
                    this,
                    tr("Manual Update Required"),
                    tr("The official download page for %1 was opened in your browser.\n\n"
                       "Please download the latest version, extract/install it, and use the 'Browse...' button to select the new executable.")
                        .arg(displayName(binaryName)));
                return;
            }

            manager = tr("Standalone");
            updateProgram = foundBinary.path;

            if (binaryName == QStringLiteral("deno")) {
                updateArgs = {QStringLiteral("upgrade")};
            } else {
                updateArgs = {QStringLiteral("-U")};
            }
        }

        if (!manager.isEmpty()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle(tr("Update %1").arg(displayName(binaryName)));
            msgBox.setTextFormat(Qt::RichText);

            QString cmdPreview = updateProgram;
            for (const QString &arg : updateArgs) {
                if (arg.contains(QLatin1Char(' '))) {
                    cmdPreview += QStringLiteral(" \"%1\"").arg(arg);
                } else {
                    cmdPreview += QStringLiteral(" %1").arg(arg);
                }
            }

            QString messageText;
            if (isStandalone) {
                messageText = tr("Would you like to run the built-in updater for <b>%1</b>?<br><br>"
                                 "<code>%2</code>")
                                  .arg(displayName(binaryName), cmdPreview);
            } else {
                messageText = tr("LzyDownloader detected that %1 is managed by <b>%2</b>.<br><br>"
                                 "Would you like to run the following update command now?<br><br>"
                                 "<code>%3</code>")
                                  .arg(displayName(binaryName), manager, cmdPreview);
            }

            msgBox.setText(messageText);
            msgBox.setIcon(QMessageBox::Question);

            QPushButton *runButton = msgBox.addButton(tr("Run Update"), QMessageBox::AcceptRole);
            msgBox.addButton(QMessageBox::Cancel);

            msgBox.exec();

            if (msgBox.clickedButton() == runButton) {
                ProcessRunOptions opts;
                opts.dialogTitle = tr("Updating %1 via %2").arg(displayName(binaryName), manager);
                opts.program = updateProgram;
                opts.arguments = updateArgs;
                opts.binaryName = binaryName;
                opts.isAlias = false;
                opts.isUpdate = true;

                runProcessWithLog(opts);
            }
        } else {
            installBinaryFor(binaryName);
        }
    });
}

void BinariesPage::fetchBinaryVersion(const QString &binaryName, const QString &path) {
    if (path.isEmpty() || !m_versionLabels.contains(binaryName)) {
        return;
    }

    m_versionLabels[binaryName]->setText(tr("Version: Fetching..."));

    BaseBinaryUpdater *updater = m_updaters.value(binaryName);
    if (!updater) {
        updater = new BaseBinaryUpdater(binaryName, QString(), m_configManager, this);
        connect(updater, &BaseBinaryUpdater::versionFetched, this, [this, binaryName](const QString &version) {
            m_versionLabels[binaryName]->setText(tr("Version: %1").arg(version));
        });
        m_updaters.insert(binaryName, updater);
    }

    updater->fetchLocalVersionOnly();
}

QString BinariesPage::browseBinary(const QString &title) const {
    QString filter;
#ifdef Q_OS_WIN
    filter = tr("Executables (*.exe);;All Files (*.*)");
#else
    filter = tr("All Files (*)");
#endif
    return QFileDialog::getOpenFileName(const_cast<BinariesPage *>(this), title, QString(), filter);
}

void BinariesPage::browseBinaryFor(const QString &binaryName) {
    const QString path = browseBinary(tr("Select %1 executable").arg(displayName(binaryName)));
    if (!path.isEmpty()) {
        saveBinaryOverride(binaryName, path);
    }
}

void BinariesPage::installBinaryFor(const QString &binaryName) {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Install %1").arg(displayName(binaryName)));
    dialog.setToolTip(tr("Choose how to install %1.").arg(displayName(binaryName)));

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QList<InstallOption> options = buildInstallOptions(binaryName);

    QLabel *infoLabel = new QLabel(&dialog);
    if (options.isEmpty()) {
        infoLabel->setText(tr("No supported package managers were detected for %1. Please use the 'Download from Official Website' option.").arg(displayName(binaryName)));
    } else {
        infoLabel->setText(tr("Select an installation method for %1. Package-manager options were detected from this system.").arg(displayName(binaryName)));
    }
    infoLabel->setWordWrap(true);
    infoLabel->setToolTip(tr("Package-manager commands are launched through the system shell (cmd.exe on Windows)."));
    layout->addWidget(infoLabel);

    InstallOption manualOpt;
    manualOpt.label = tr("Download from Official Website");
    manualOpt.description = tr("Open the official download page in your web browser and show manual placement instructions.");
    manualOpt.extraData.insert(QStringLiteral("is_manual_download"), true);
    options.append(manualOpt);

    QComboBox *optionsCombo = new QComboBox(&dialog);
    optionsCombo->setToolTip(tr("Choose an installation method."));
    for (const InstallOption &option : options) {
        optionsCombo->addItem(option.label);
    }
    layout->addWidget(optionsCombo);

    QLabel *descriptionLabel = new QLabel(&dialog);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setToolTip(tr("Summary of the selected install method."));
    layout->addWidget(descriptionLabel);

    QLabel *commandLabel = new QLabel(&dialog);
    commandLabel->setWordWrap(true);
    commandLabel->setMaximumWidth(550);
    commandLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    commandLabel->setToolTip(tr("Command that will be launched when you choose Run Install Command."));
    layout->addWidget(commandLabel);

    auto updateSelectionText = [&]() {
        const InstallOption &option = options.at(optionsCombo->currentIndex());
        descriptionLabel->setText(option.description);
        if (option.extraData.value(QStringLiteral("is_manual_download")).toBool()) {
            commandLabel->setText(tr("Command: Opens in default web browser"));
        } else {
                commandLabel->setText(tr("<div style='word-break: break-all;'><b>Command:</b> %1</div>").arg(commandPreview(option)));
        }
    };

    connect(optionsCombo, &QComboBox::currentIndexChanged, &dialog, updateSelectionText);
    updateSelectionText();

    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    QPushButton *runButton = buttons->addButton(tr("Run Install"), QDialogButtonBox::AcceptRole);
    QPushButton *cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    runButton->setToolTip(tr("Execute the selected installation method."));
    cancelButton->setToolTip(tr("Close this installer dialog."));
    layout->addWidget(buttons);

    connect(runButton, &QPushButton::clicked, &dialog, [&]() {
        const InstallOption &option = options.at(optionsCombo->currentIndex());

        if (option.extraData.value(QStringLiteral("is_manual_download")).toBool()) {
            QDesktopServices::openUrl(QUrl(m_manualUrls.value(binaryName)));
            QMessageBox::information(
                &dialog,
                tr("Browser Download"),
                tr("The official download page for %1 was opened in your browser.\n\n"
                        "After downloading, place the executable somewhere permanent and use Browse to point LzyDownloader to it.")
                    .arg(displayName(binaryName)));
            dialog.accept();
            return;
        }

        ProcessRunOptions opts;
        opts.dialogTitle = tr("Installing %1").arg(displayName(binaryName));
        opts.program = option.program;
        opts.arguments = option.arguments;
        opts.binaryName = binaryName;
        opts.isAlias = option.extraData.value(QStringLiteral("is_windows_apps_alias")).toBool();
        if (option.extraData.contains(QStringLiteral("set_custom_path"))) {
            opts.setCustomPath = option.extraData.value(QStringLiteral("set_custom_path")).toString();
        }
        opts.isUpdate = false;

        runProcessWithLog(opts);
        dialog.accept();
    });

    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void BinariesPage::runProcessWithLog(const ProcessRunOptions &opts) {
    QDialog progressDialog(this);
    progressDialog.setWindowTitle(opts.dialogTitle);
    progressDialog.resize(600, 400);

    QVBoxLayout *pLayout = new QVBoxLayout(&progressDialog);

    QTextEdit *outputEdit = new QTextEdit(&progressDialog);
    outputEdit->setReadOnly(true);
    outputEdit->setFontFamily(QStringLiteral("Courier New"));
    outputEdit->setWordWrapMode(QTextOption::WrapAnywhere);
    pLayout->addWidget(outputEdit);

    QDialogButtonBox *pButtons = new QDialogButtonBox(QDialogButtonBox::Close, &progressDialog);
    QPushButton *closeBtn = pButtons->button(QDialogButtonBox::Close);
    closeBtn->setEnabled(false);
    pLayout->addWidget(pButtons);

    connect(pButtons, &QDialogButtonBox::rejected, &progressDialog, &QDialog::reject);

    QProcess *process = new QProcess(&progressDialog);
    ProcessUtils::setProcessEnvironment(*process);
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(&progressDialog, &QDialog::rejected, process, [process]() {
        if (process->state() != QProcess::NotRunning) {
            process->setProperty("cancelled", QVariant(true));
            ProcessUtils::terminateProcessTree(process);
            process->kill();
        }
    });

    if (opts.isAlias) {
        QProcessEnvironment env = process->processEnvironment();
        const QString windowsAppsPath = getWindowsAppsDir();
        if (!windowsAppsPath.isEmpty()) {
            const QString currentPath = env.value(QStringLiteral("PATH"));
            env.insert(QStringLiteral("PATH"), QStringLiteral("%1;%2").arg(windowsAppsPath, currentPath));
            process->setProcessEnvironment(env);
        }
    }

    QStringList commandParts;
    if (opts.program.contains(QLatin1Char(' '))) {
        commandParts << QStringLiteral("\"%1\"").arg(opts.program);
    } else {
        commandParts << opts.program;
    }
    for (const QString &arg : opts.arguments) {
        if (arg.contains(QLatin1Char(' '))) {
            commandParts << QStringLiteral("\"%1\"").arg(arg);
        } else {
            commandParts << arg;
        }
    }
    const QString fullCommand = commandParts.join(QLatin1Char(' '));

    QPointer<QDialog> pDialog(&progressDialog);

    connect(process, &QProcess::readyReadStandardOutput, process, [process, outputEdit, pDialog]() {
        if (!pDialog) return;
        QByteArray buffer = process->property("outputBuffer").toByteArray();
        buffer.append(process->readAllStandardOutput());
        qsizetype lastDelimiter = qMax(buffer.lastIndexOf('\n'), buffer.lastIndexOf('\r'));
        if (lastDelimiter != -1) {
            QString output = QString::fromUtf8(buffer.constData(), lastDelimiter + 1);
            buffer.remove(0, lastDelimiter + 1);
            outputEdit->moveCursor(QTextCursor::End);
            outputEdit->insertPlainText(output);
            outputEdit->moveCursor(QTextCursor::End);
        }
        process->setProperty("outputBuffer", buffer);
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process, outputEdit, closeBtn, opts, pDialog](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!pDialog) return;
        if (process->property("cancelled").toBool()) return;
        closeBtn->setEnabled(true);
        QByteArray buffer = process->property("outputBuffer").toByteArray();
        if (!buffer.isEmpty()) {
            outputEdit->moveCursor(QTextCursor::End);
            outputEdit->insertPlainText(QString::fromUtf8(buffer));
        }
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            outputEdit->moveCursor(QTextCursor::End);
            outputEdit->insertPlainText(tr("\n--- Process completed successfully. ---\n"));

            m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(opts.binaryName), false);
            if (opts.binaryName == QStringLiteral("ffmpeg")) {
                m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("ffprobe_update_available"), false);
            } else if (opts.binaryName == QStringLiteral("ffprobe")) {
                m_configManager->set(QStringLiteral("Binaries"), QStringLiteral("ffmpeg_update_available"), false);
            }
            m_configManager->save();

            if (!opts.setCustomPath.isEmpty()) {
#ifndef Q_OS_WIN
                if (QFile::exists(opts.setCustomPath)) {
                    QFile::setPermissions(opts.setCustomPath, QFile::permissions(opts.setCustomPath) | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                }
#endif
                this->saveBinaryOverride(opts.binaryName, opts.setCustomPath);
                
                if (opts.binaryName == QStringLiteral("ffmpeg") || opts.binaryName == QStringLiteral("ffprobe")) {
                    QFileInfo binInfo(opts.setCustomPath);
                    QString siblingName = (opts.binaryName == QStringLiteral("ffmpeg")) ? QStringLiteral("ffprobe.exe") : QStringLiteral("ffmpeg.exe");
                    QString siblingPath = binInfo.dir().filePath(siblingName);
                    QString siblingKey = (opts.binaryName == QStringLiteral("ffmpeg")) ? QStringLiteral("ffprobe") : QStringLiteral("ffmpeg");
                    this->saveBinaryOverride(siblingKey, siblingPath);
                }
            }

            ProcessUtils::clearCache();
            this->setBinaryWarning(opts.binaryName, QString());
            this->refreshBinaryStatus(opts.binaryName);
            if (opts.binaryName == QStringLiteral("ffmpeg")) this->refreshBinaryStatus(QStringLiteral("ffprobe"));
            else if (opts.binaryName == QStringLiteral("ffprobe")) this->refreshBinaryStatus(QStringLiteral("ffmpeg"));

            if (!opts.isUpdate) {
                const ProcessUtils::FoundBinary refreshedBinary = ProcessUtils::findBinary(opts.binaryName, m_configManager);
                if (pDialog) {
                    if (refreshedBinary.source == QStringLiteral("Not Found") || refreshedBinary.source == QStringLiteral("Invalid Custom")) {
                        QMessageBox::information(
                            pDialog,
                            tr("Install Finished"),
                            tr("%1 finished installing, but LzyDownloader could not detect it yet.\n\n"
                               "Use Refresh after closing this dialog. If it is still missing, restart LzyDownloader or use Browse to select the executable.")
                                .arg(displayName(opts.binaryName)));
                    } else {
                        QMessageBox::information(
                            pDialog,
                            tr("Install Successful"),
                            tr("%1 has been installed and detected successfully.").arg(displayName(opts.binaryName)));
                    }
                }
            }
        } else {
            outputEdit->moveCursor(QTextCursor::End);
            outputEdit->insertPlainText(tr("\n--- Process failed with exit code %1. ---\n").arg(exitCode));

            const QString logText = outputEdit->toPlainText();
            const QString actionName = opts.isUpdate ? tr("update") : tr("installation");
            if (pDialog) {
                if (logText.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive) || logText.contains(QStringLiteral("Access is denied"), Qt::CaseInsensitive)) {
                    QMessageBox::warning(pDialog, tr("Permission Denied"),
                        tr("The %1 of %2 failed due to insufficient permissions.\n\n"
                           "If you are trying to install or update in a protected system folder (like 'Program Files'), "
                           "please restart LzyDownloader as Administrator and try again.").arg(actionName, displayName(opts.binaryName)));
                } else {
                    QMessageBox::warning(pDialog, tr("Process Failed"), tr("The %1 of %2 failed. Please check the output log.").arg(actionName, displayName(opts.binaryName)));
                }
            }
        }
    });

    connect(process, &QProcess::errorOccurred, process, [process, outputEdit, closeBtn, pDialog](QProcess::ProcessError) {
        if (!pDialog) return;
        closeBtn->setEnabled(true);
        outputEdit->moveCursor(QTextCursor::End);
        outputEdit->insertPlainText(tr("\n--- Process error: %1 ---\n").arg(process->errorString()));
    });

    outputEdit->insertPlainText(tr("Running command: %1\n\n").arg(fullCommand));
#ifdef Q_OS_WIN
    if (opts.isUpdate) {
        const bool isStandalone = !opts.program.toLower().contains(QStringLiteral("scoop")) &&
                                  !opts.program.toLower().contains(QStringLiteral("winget")) &&
                                  !opts.program.toLower().contains(QStringLiteral("pip")) &&
                                  !opts.program.toLower().contains(QStringLiteral("brew")) &&
                                  !opts.program.toLower().contains(QStringLiteral("choco"));
        if (isStandalone) {
            process->start(opts.program, opts.arguments);
        } else {
            process->start(QStringLiteral("cmd"), {QStringLiteral("/C"), fullCommand});
        }
    } else {
        QStringList cmdArgs;
        cmdArgs << QStringLiteral("/C") << fullCommand;
        process->start(QStringLiteral("cmd"), cmdArgs);
    }
#else
    process->start(opts.program, opts.arguments);
#endif

    process->closeWriteChannel();
    progressDialog.exec();
}

void BinariesPage::setBinaryWarning(const QString &binaryName, const QString &details) {
    if (details.isEmpty()) {
        m_binaryWarnings.remove(binaryName);
    } else {
        m_binaryWarnings.insert(binaryName, details);
    }
    refreshBinaryStatus(binaryName);
}

void BinariesPage::saveBinaryOverride(const QString &binaryName, const QString &path) {
    qInfo() << "[BinariesPage::saveBinaryOverride] Override path updated for:" << binaryName << "to:" << path;
    const QString configKey = m_configKeys.value(binaryName);
    if (configKey.isEmpty()) {
        return;
    }

    ProcessUtils::clearCache();
    m_binaryWarnings.remove(binaryName);

    QStringList keysToClear;
    keysToClear << QStringLiteral("Binaries/%1_update_available").arg(binaryName)
                << QStringLiteral("Binaries/%1_latest_version").arg(binaryName);

    if (path.isEmpty()) {
        keysToClear << QStringLiteral("Binaries/%1").arg(configKey);
        if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
            const QString siblingName = (binaryName == QStringLiteral("ffmpeg")) ? QStringLiteral("ffprobe") : QStringLiteral("ffmpeg");
            const QString siblingKey = (binaryName == QStringLiteral("ffmpeg")) ? QStringLiteral("ffprobe_path") : QStringLiteral("ffmpeg_path");
            keysToClear << QStringLiteral("Binaries/%1").arg(siblingKey)
                        << QStringLiteral("Binaries/%1_update_available").arg(siblingName)
                        << QStringLiteral("Binaries/%1_latest_version").arg(siblingName);
            m_binaryWarnings.remove(siblingName);
        }
    }

    for (const QString &keyPath : keysToClear) {
        const QStringList parts = keyPath.split(QLatin1Char('/'));
        if (parts.size() == 2) {
            m_configManager->remove(parts[0], parts[1]);
        }
    }

    for (QSettings::Format format : {QSettings::NativeFormat, QSettings::IniFormat}) {
        QSettings settings(format, QSettings::UserScope, QStringLiteral("LzyDownloader"), QStringLiteral("LzyDownloader"));
        for (const QString &keyPath : keysToClear) {
            settings.remove(keyPath);
        }
        settings.sync();
    }

    if (!path.isEmpty()) {
        m_configManager->set(QStringLiteral("Binaries"), configKey, path);
    }
    m_configManager->save();
}

void BinariesPage::loadSettings() {
    for (auto it = m_statusLabels.constBegin(); it != m_statusLabels.constEnd(); ++it) {
        refreshBinaryStatus(it.key());
    }
}

void BinariesPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section != QStringLiteral("Binaries")) {
        return;
    }

    const QString binaryName = m_configKeys.key(key);
    if (binaryName.isEmpty() || !m_statusLabels.contains(binaryName)) {
        return;
    }

    refreshBinaryStatus(binaryName);
}

void BinariesPage::refreshBinaryStatus(const QString &binaryName) {
    QLabel *statusLabel = m_statusLabels.value(binaryName);
    QPushButton *installButton = m_installButtons.value(binaryName);
    QLabel *versionLabel = m_versionLabels.value(binaryName);
    QPushButton *updateButton = m_updateButtons.value(binaryName);
    QPushButton *clearButton = this->findChild<QPushButton*>(QStringLiteral("%1_clearButton").arg(binaryName));

    if (!statusLabel || !installButton || !versionLabel || !updateButton) {
        return;
    }

    // Use findBinary to benefit from cached resolutions
    const ProcessUtils::FoundBinary foundBinary = SmartBinaryResolver::resolve(binaryName, m_configManager);

    const bool hasWarning = m_binaryWarnings.contains(binaryName) || m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), false).toBool();
    QString warningDetails = m_binaryWarnings.value(binaryName);
    if (warningDetails.isEmpty() && m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("%1_update_available").arg(binaryName), false).toBool()) {
        QString latestVer = m_configManager->get(QStringLiteral("Binaries"), QStringLiteral("%1_latest_version").arg(binaryName)).toString();
        if (!latestVer.isEmpty()) {
            warningDetails = tr("A newer version (%1) is available.").arg(latestVer);
        } else {
            warningDetails = tr("An update is available.");
        }
    }

    qDebug() << "[BinariesPage] refreshBinaryStatus:" << binaryName << "- source:" << foundBinary.source << "| path:" << foundBinary.path;

    QString displayPath = foundBinary.path.toHtmlEscaped();
    // Insert a zero-width space after path separators so QLabel can properly word-wrap long paths without spaces
    displayPath.replace(QStringLiteral("\\"), QStringLiteral("\\\u200B"));
    displayPath.replace(QStringLiteral("/"), QStringLiteral("/\u200B"));

    const bool isInstalled = (foundBinary.source != QStringLiteral("Not Found") && foundBinary.source != QStringLiteral("Invalid Custom"));
    const bool hasCustomPath = !m_configManager->get(QStringLiteral("Binaries"), m_configKeys.value(binaryName)).toString().isEmpty();

    const QString pathLower = foundBinary.path.toLower();
    const bool isPackageManaged = pathLower.contains(QStringLiteral("scoop")) || pathLower.contains(QStringLiteral("windowsapps")) ||
                            pathLower.contains(QStringLiteral("python")) || pathLower.contains(QStringLiteral("pip")) || pathLower.contains(QStringLiteral("scripts")) || pathLower.contains(QStringLiteral("site-packages")) ||
                            pathLower.contains(QStringLiteral("homebrew")) || pathLower.contains(QStringLiteral("cellar")) || pathLower.contains(QStringLiteral("linuxbrew")) ||
                            pathLower.contains(QStringLiteral("chocolatey")) || pathLower.contains(QStringLiteral("choco"));
    const bool supportsUpdate = true;

    // Re-enable in case they were disabled by "Refresh All Statuses"
    installButton->setEnabled(true);
    updateButton->setEnabled(true);
    if (clearButton) {
        clearButton->setEnabled(hasCustomPath);
    }

    // Standardize button visibility across all binaries
    installButton->setVisible(!isInstalled);
    updateButton->setVisible(isInstalled && supportsUpdate && hasWarning);
    if (clearButton) {
        clearButton->setVisible(true);
    }

    if (foundBinary.source == QStringLiteral("Not Found")) {
        const bool isOptional = m_optionalBinaries.contains(binaryName);
        const QString requiredText = isOptional ? tr("This is an optional enhancement.") : tr("This binary is required for core functionality.");

        const QString statusPrefix = isOptional
            ? QStringLiteral("<span style='color: #d97706;'>Missing optional tool</span>")
            : QStringLiteral("<span style='color: #dc2626;'>Missing required tool</span>");

        statusLabel->setText(tr("<b>Status:</b> %1. %2").arg(statusPrefix, requiredText));
        versionLabel->setText(tr("Version: Unknown"));
    } else if (foundBinary.source == QStringLiteral("Invalid Custom")) {
        const bool isOptional = m_optionalBinaries.contains(binaryName);
        const QString statusPrefix = isOptional
            ? QStringLiteral("<span style='color: #d97706;'>Manual path not found for optional tool</span>")
            : QStringLiteral("<span style='color: #dc2626;'>Manual path not found for required tool</span>");
        statusLabel->setText(tr("<b>Status:</b> %1 (invalid manual override)<br><b>Path:</b> %2").arg(statusPrefix, displayPath));
        versionLabel->setText(tr("Version: Unknown"));
    } else if (foundBinary.source == QStringLiteral("Custom")) {
        if (hasWarning) {
            const QString statusPrefix = QStringLiteral("<span style='color: #d97706;'>Update Recommended</span>");
            statusLabel->setText(tr("<b>Status:</b> %1 (manual override)<br><b>Details:</b> %2<br><b>Path:</b> %3").arg(statusPrefix, warningDetails.toHtmlEscaped(), displayPath));
        } else {
            const QString statusPrefix = QStringLiteral("<span style='color: #16a34a;'>Found</span>");
            statusLabel->setText(tr("<b>Status:</b> %1 (manual override)<br><b>Path:</b> %2").arg(statusPrefix, displayPath));
        }
        fetchBinaryVersion(binaryName, foundBinary.path);
    } else {
        if (hasWarning) {
            const QString statusPrefix = QStringLiteral("<span style='color: #d97706;'>Update Recommended</span>");
            statusLabel->setText(tr("<b>Status:</b> %1 (auto-detected via %2)<br><b>Details:</b> %3<br><b>Path:</b> %4").arg(statusPrefix, foundBinary.source, warningDetails.toHtmlEscaped(), displayPath));
        } else {
            const QString statusPrefix = QStringLiteral("<span style='color: #16a34a;'>Found</span>");
            statusLabel->setText(tr("<b>Status:</b> %1 (auto-detected via %2)<br><b>Path:</b> %3").arg(statusPrefix, foundBinary.source, displayPath));
        }
        fetchBinaryVersion(binaryName, foundBinary.path);
    }
}

QList<BinariesPage::InstallOption> BinariesPage::buildInstallOptions(const QString &binaryName) const {
    QList<InstallOption> options;
    const QString display = displayName(binaryName);

    // OS-gating so users don't see macOS/Linux-only managers on Windows and vice versa.
    auto isWindows = []() -> bool {
#ifdef Q_OS_WIN
        return true;
#else
        return false;
#endif
    };
    auto isMacOS = []() -> bool {
#ifdef Q_OS_MACOS
        return true;
#else
        return false;
#endif
    };
    auto isLinux = []() -> bool {
#ifdef Q_OS_LINUX
        return true;
#else
        return false;
#endif
    };

    auto addOptionIfPresent = [&](const QString &program, const QStringList &arguments, const QString &description) {
        QString programPath;
        bool isWindowsAppsAlias = false;

        // First try standard PATH lookup
        QString foundPath = QStandardPaths::findExecutable(program);
        if (!foundPath.isEmpty()) {
            programPath = foundPath;
        }

        // On Windows, many tools live in the WindowsApps execution-alias directory
        // which is NOT in PATH. These are 0-byte stubs that only work through shell
        // alias resolution — they crash if invoked by full path directly.
        // Mark them so the launch code can handle them correctly.
        if (programPath.isEmpty()) {
            const QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
            if (!localAppData.isEmpty()) {
                const QString windowsApps = QDir(localAppData).filePath(QStringLiteral("Microsoft/WindowsApps/%1.exe").arg(program));
                if (QFile::exists(windowsApps)) {
                    // Don't store the full path — keep the bare name and flag as alias.
                    // The launch code will prepend WindowsApps to PATH so the alias
                    // resolves correctly via shell magic.
                    programPath = program;
                    isWindowsAppsAlias = true;
                }
            }
        }

        if (programPath.isEmpty()) {
            return;
        }

        InstallOption option;
        option.label = tr("%1 (%2)").arg(program, display);
        option.description = description;
        option.program = programPath;
        option.arguments = arguments;
        option.extraData.insert(QStringLiteral("is_windows_apps_alias"), isWindowsAppsAlias);
        options.append(option);
    };

    if (binaryName == QStringLiteral("yt-dlp")) {
        // Direct GitHub nightly download via curl
        const QString curlPath = QStandardPaths::findExecutable(QStringLiteral("curl"));
        if (!curlPath.isEmpty()) {
            InstallOption opt;
            opt.label = tr("curl (yt-dlp nightly)");
            opt.extraData.insert(QStringLiteral("is_windows_apps_alias"), false);

            if (isWindows()) {
                const QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
                if (!localAppData.isEmpty()) {
                    const QString targetPath = QDir(localAppData).filePath(QStringLiteral("LzyDownloader/bin/yt-dlp.exe"));
                    opt.description = tr("Recommended on Windows. Download the latest nightly yt-dlp standalone executable directly from GitHub to your user directory.");
                    opt.program = curlPath;
                    opt.arguments = {QStringLiteral("-L"), QStringLiteral("--create-dirs"), QStringLiteral("-o"), targetPath, QStringLiteral("https://github.com/yt-dlp/yt-dlp-nightly-builds/releases/latest/download/yt-dlp.exe")};
                    opt.extraData.insert(QStringLiteral("set_custom_path"), targetPath);
                    options.append(opt);
                }
            } else {
                const QString targetDir = QDir(QDir::homePath()).filePath(QStringLiteral(".local/bin"));
                const QString targetPath = QDir(targetDir).filePath(QStringLiteral("yt-dlp"));
                QString downloadUrl = QStringLiteral("https://github.com/yt-dlp/yt-dlp-nightly-builds/releases/latest/download/yt-dlp");
                if (isMacOS()) downloadUrl = QStringLiteral("https://github.com/yt-dlp/yt-dlp-nightly-builds/releases/latest/download/yt-dlp_macos");
                else if (isLinux()) downloadUrl = QStringLiteral("https://github.com/yt-dlp/yt-dlp-nightly-builds/releases/latest/download/yt-dlp_linux");

                opt.description = tr("Download the latest nightly yt-dlp executable directly from GitHub to ~/.local/bin.");
                opt.program = curlPath;
                opt.arguments = {QStringLiteral("-L"), QStringLiteral("--create-dirs"), QStringLiteral("-o"), targetPath, downloadUrl};
                opt.extraData.insert(QStringLiteral("set_custom_path"), targetPath);
                options.append(opt);
            }
        }

        if (isWindows()) addOptionIfPresent(QStringLiteral("scoop"), {QStringLiteral("install"), QStringLiteral("yt-dlp-nightly")}, tr("Install yt-dlp (nightly) with Scoop. Requires the 'extras' bucket (`scoop bucket add extras`)."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("winget"), {QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("yt-dlp.yt-dlp"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")}, tr("Install yt-dlp (stable) with WinGet."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("choco"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("yt-dlp")}, tr("Install yt-dlp (stable) with Chocolatey."));
        if (isMacOS() || isLinux()) addOptionIfPresent(QStringLiteral("brew"), {QStringLiteral("install"), QStringLiteral("yt-dlp"), QStringLiteral("--HEAD")}, tr("Install yt-dlp (latest from master) with Homebrew."));
    } else if (binaryName == QStringLiteral("ffmpeg") || binaryName == QStringLiteral("ffprobe")) {
        if (isWindows()) {
            const QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
            if (!localAppData.isEmpty()) {
                const QString targetPath = QDir(localAppData).filePath(QStringLiteral("LzyDownloader/bin/%1.exe").arg(binaryName));
                InstallOption opt;
                opt.label = tr("PowerShell (FFmpeg/FFprobe stable essentials)");
                opt.description = tr("Download and extract the latest stable FFmpeg and FFprobe binaries directly to LzyDownloader's local bin folder.");
                opt.program = QStringLiteral("powershell");
                
                QString rawScript = QStringLiteral(
                    "$ErrorActionPreference = 'Stop'; "
                    "$progressPreference = 'SilentlyContinue'; "
                    "[Net.ServicePointManager]::SecurityProtocol = 'Tls12, Tls13'; "
                    "$binDir = '%1'; "
                    "if (!(Test-Path $binDir)) { New-Item -ItemType Directory -Path $binDir -Force }; "
                    "$zipPath = Join-Path $env:TEMP 'ffmpeg.zip'; "
                    "Write-Host 'Downloading FFmpeg release...'; "
                    "Invoke-WebRequest -Uri 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip' -OutFile $zipPath; "
                    "Write-Host 'Extracting archive...'; "
                    "$extractDir = Join-Path $env:TEMP 'ffmpeg_extracted'; "
                    "if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }; "
                    "Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force; "
                    "$ffmpegExe = Get-ChildItem -Path $extractDir -Filter 'ffmpeg.exe' -Recurse | Select-Object -First 1; "
                    "$ffprobeExe = Get-ChildItem -Path $extractDir -Filter 'ffprobe.exe' -Recurse | Select-Object -First 1; "
                    "if ($ffmpegExe -and $ffprobeExe) { "
                    "  Copy-Item -Path $ffmpegExe.FullName -Destination $binDir -Force; "
                    "  Copy-Item -Path $ffprobeExe.FullName -Destination $binDir -Force; "
                    "  Write-Host 'FFmpeg and FFprobe installed successfully!'; "
                    "} else { "
                    "  throw 'Failed to locate binaries inside archive'; "
                    "}; "
                    "Remove-Item -Force $zipPath; "
                    "Remove-Item -Recurse -Force $extractDir"
                ).arg(QDir::toNativeSeparators(QDir(localAppData).filePath(QStringLiteral("LzyDownloader/bin"))));
                
                // Convert raw script to UTF-16LE and encode to Base64 to prevent shell parsing errors
                QByteArray utf16Bytes;
                utf16Bytes.resize(rawScript.length() * 2);
                memcpy(utf16Bytes.data(), rawScript.utf16(), rawScript.length() * 2);
                QString encodedCmd = QString::fromLatin1(utf16Bytes.toBase64());
                
                opt.arguments = {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-EncodedCommand"), encodedCmd};
                opt.extraData.insert(QStringLiteral("is_windows_apps_alias"), false);
                opt.extraData.insert(QStringLiteral("set_custom_path"), targetPath);
                options.append(opt);
            }
        }

        if (isWindows()) addOptionIfPresent(QStringLiteral("winget"), {QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("Gyan.FFmpeg"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")}, tr("Install FFmpeg (includes ffprobe) with WinGet."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("choco"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("ffmpeg")}, tr("Install FFmpeg (includes ffprobe) with Chocolatey."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("scoop"), {QStringLiteral("install"), QStringLiteral("ffmpeg")}, tr("Install FFmpeg (includes ffprobe) with Scoop."));
        if (isLinux()) addOptionIfPresent(QStringLiteral("apt"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("ffmpeg")}, tr("Install FFmpeg (includes ffprobe) with apt."));
        if (isLinux()) addOptionIfPresent(QStringLiteral("dnf"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("ffmpeg")}, tr("Install FFmpeg (includes ffprobe) with dnf."));
        if (isLinux()) addOptionIfPresent(QStringLiteral("pacman"), {QStringLiteral("-S"), QStringLiteral("--noconfirm"), QStringLiteral("ffmpeg")}, tr("Install FFmpeg (includes ffprobe) with pacman."));
        if (isMacOS() || isLinux()) addOptionIfPresent(QStringLiteral("brew"), {QStringLiteral("install"), QStringLiteral("ffmpeg")}, tr("Install FFmpeg (includes ffprobe) with Homebrew."));
    } else if (binaryName == QStringLiteral("gallery-dl")) {
        // Direct GitHub standalone download via curl (Windows only)
        if (isWindows()) {
            const QString curlPath = QStandardPaths::findExecutable(QStringLiteral("curl"));
            if (!curlPath.isEmpty()) {
                const QString localAppData = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LOCALAPPDATA"));
                if (!localAppData.isEmpty()) {
                    const QString targetPath = QDir(localAppData).filePath(QStringLiteral("LzyDownloader/bin/gallery-dl.exe"));
                    InstallOption opt;
                    opt.label = tr("curl (gallery-dl standalone)");
                    opt.description = tr("Recommended on Windows. Download the latest gallery-dl standalone executable directly from GitHub to your user directory.");
                    opt.program = curlPath;
                    opt.arguments = {QStringLiteral("-L"), QStringLiteral("--create-dirs"), QStringLiteral("-o"), targetPath, QStringLiteral("https://github.com/mikf/gallery-dl/releases/latest/download/gallery-dl.exe")};
                    opt.extraData.insert(QStringLiteral("is_windows_apps_alias"), false);
                    opt.extraData.insert(QStringLiteral("set_custom_path"), targetPath);
                    options.append(opt);
                }
            }
        }

        if (isWindows()) addOptionIfPresent(QStringLiteral("winget"), {QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("mikf.gallery-dl"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")}, tr("Install gallery-dl (stable) with WinGet."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("winget"), {QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("mikf.gallery-dl.Nightly"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")}, tr("Install gallery-dl (nightly) with WinGet."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("choco"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("gallery-dl")}, tr("Install gallery-dl with Chocolatey."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("scoop"), {QStringLiteral("install"), QStringLiteral("gallery-dl")}, tr("Install gallery-dl with Scoop."));
        if (isMacOS() || isLinux()) addOptionIfPresent(QStringLiteral("brew"), {QStringLiteral("install"), QStringLiteral("gallery-dl")}, tr("Install gallery-dl with Homebrew."));
    } else if (binaryName == QStringLiteral("aria2c")) {
        if (isWindows()) addOptionIfPresent(QStringLiteral("winget"), {QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("aria2.aria2"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")}, tr("Install aria2 with WinGet."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("choco"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("aria2")}, tr("Install aria2 with Chocolatey."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("scoop"), {QStringLiteral("install"), QStringLiteral("aria2")}, tr("Install aria2 with Scoop."));
        if (isLinux()) addOptionIfPresent(QStringLiteral("apt"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("aria2")}, tr("Install aria2 with apt."));
        if (isLinux()) addOptionIfPresent(QStringLiteral("dnf"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("aria2")}, tr("Install aria2 with dnf."));
        if (isLinux()) addOptionIfPresent(QStringLiteral("pacman"), {QStringLiteral("-S"), QStringLiteral("--noconfirm"), QStringLiteral("aria2")}, tr("Install aria2 with pacman."));
        if (isMacOS() || isLinux()) addOptionIfPresent(QStringLiteral("brew"), {QStringLiteral("install"), QStringLiteral("aria2")}, tr("Install aria2 with Homebrew."));
    } else if (binaryName == QStringLiteral("deno")) {
        if (isWindows()) addOptionIfPresent(QStringLiteral("winget"), {QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("DenoLand.Deno"), QStringLiteral("--exact"), QStringLiteral("--accept-package-agreements"), QStringLiteral("--accept-source-agreements")}, tr("Install Deno with WinGet."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("choco"), {QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("deno")}, tr("Install Deno with Chocolatey."));
        if (isWindows()) addOptionIfPresent(QStringLiteral("scoop"), {QStringLiteral("install"), QStringLiteral("deno")}, tr("Install Deno with Scoop."));
        if (isMacOS() || isLinux()) addOptionIfPresent(QStringLiteral("brew"), {QStringLiteral("install"), QStringLiteral("deno")}, tr("Install Deno with Homebrew."));
    }

    return options;
}

QString BinariesPage::commandPreview(const InstallOption &option) const {
    QStringList commandParts;
    if (option.program.contains(QLatin1Char(' '))) {
        commandParts << QStringLiteral("\"%1\"").arg(option.program);
    } else {
        commandParts << option.program;
    }
    for (const QString &arg : option.arguments) {
        if (arg.contains(QLatin1Char(' '))) {
            commandParts << QStringLiteral("\"%1\"").arg(arg);
        } else {
            commandParts << arg;
        }
    }
    return commandParts.join(QLatin1Char(' '));
}

QString BinariesPage::displayName(const QString &binaryName) const {
    return m_displayNames.value(binaryName, binaryName);
}

void BinariesPage::setGalleryDlVersion(const QString &version) {
    if (m_versionLabels.contains(QStringLiteral("gallery-dl"))) {
        m_versionLabels[QStringLiteral("gallery-dl")] ->setText(tr("Version: %1").arg(version));
    }
}

void BinariesPage::setYtDlpVersion(const QString &version) {
    if (m_versionLabels.contains(QStringLiteral("yt-dlp"))) {
        m_versionLabels[QStringLiteral("yt-dlp")] ->setText(tr("Version: %1").arg(version));
    }
}
