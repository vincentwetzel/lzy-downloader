#include "InitialBinarySetupDialog.h"

#include "core/ConfigManager.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {
const QStringList kRequiredBinaries = {
    QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"),
    QStringLiteral("ffprobe"), QStringLiteral("deno")
};
const QStringList kAllBinaries = {
    QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"),
    QStringLiteral("deno"), QStringLiteral("gallery-dl"), QStringLiteral("aria2c")
};

bool isAppManagedPath(const QString &path, const ConfigManager *configManager)
{
    if (!configManager || path.isEmpty()) {
        return false;
    }
    const QString managedRoot = QDir(configManager->getConfigDir()).filePath(QStringLiteral("bin"));
    return QDir::cleanPath(path).startsWith(QDir::cleanPath(managedRoot), Qt::CaseInsensitive);
}
}

InitialBinarySetupDialog::InitialBinarySetupDialog(ConfigManager *configManager,
                                                   const QStringList &missingRequired,
                                                   QWidget *parent)
    : QDialog(parent),
      m_configManager(configManager),
      m_systemFirstButton(nullptr),
      m_appManagedFirstButton(nullptr),
      m_galleryDlCheck(nullptr),
      m_aria2cCheck(nullptr)
{
    Q_UNUSED(missingRequired);
    setWindowTitle(tr("Set Up LzyDownloader"));
    setMinimumWidth(620);
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(
        tr("LzyDownloader can use tools already installed on your computer, or maintain "
           "its own copies in its private application folder. Required tools will be installed "
           "before downloads are enabled."), this);
    intro->setWordWrap(true);
    intro->setToolTip(tr("Explains the first-launch external-tool setup."));
    layout->addWidget(intro);

    auto *modeGroup = new QGroupBox(tr("Tool preference"), this);
    modeGroup->setToolTip(tr("Choose whether LzyDownloader should prefer detected system tools or its app-managed copies."));
    auto *modeLayout = new QVBoxLayout(modeGroup);
    m_systemFirstButton = new QRadioButton(tr("Use existing system tools when available"), modeGroup);
    m_systemFirstButton->setToolTip(tr("Use compatible tools already installed by you or your package manager. Missing tools are installed for LzyDownloader."));
    m_appManagedFirstButton = new QRadioButton(tr("Prefer LzyDownloader-managed tools when available"), modeGroup);
    m_appManagedFirstButton->setToolTip(tr("Use private copies in LzyDownloader's bin folder when a direct installer is available; package-manager alternatives remain available for tools without one."));
    modeLayout->addWidget(m_systemFirstButton);
    modeLayout->addWidget(m_appManagedFirstButton);

    if (hasSystemBinary()) {
        m_systemFirstButton->setChecked(true);
        layout->addWidget(modeGroup);
    } else {
        m_appManagedFirstButton->setChecked(true);
        modeGroup->setVisible(false);
    }

    auto *optionalGroup = new QGroupBox(tr("Optional tools"), this);
    optionalGroup->setToolTip(tr("Optional tools are selected by default and can be skipped if you do not need their features."));
    auto *optionalLayout = new QVBoxLayout(optionalGroup);
    m_galleryDlCheck = new QCheckBox(tr("Install gallery-dl for image and gallery downloads"), optionalGroup);
    m_galleryDlCheck->setChecked(true);
    m_galleryDlCheck->setToolTip(tr("Installs gallery-dl, which enables gallery and image downloads."));
    m_aria2cCheck = new QCheckBox(tr("Install aria2c for optional multi-connection downloading"), optionalGroup);
    m_aria2cCheck->setChecked(true);
    m_aria2cCheck->setToolTip(tr("Installs aria2c, an optional external downloader that can improve some transfers."));
    optionalLayout->addWidget(m_galleryDlCheck);
    optionalLayout->addWidget(m_aria2cCheck);
    layout->addWidget(optionalGroup);

    auto *summary = new QLabel(this);
    summary->setObjectName(QStringLiteral("binarySetupSummary"));
    summary->setWordWrap(true);
    summary->setToolTip(tr("Summarizes which tools will be installed by the selected setup choice."));
    layout->addWidget(summary);

    auto *buttons = new QDialogButtonBox(this);
    auto *continueButton = buttons->addButton(tr("Set Up Tools"), QDialogButtonBox::AcceptRole);
    continueButton->setToolTip(tr("Save this preference and install the selected tools."));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_systemFirstButton, &QRadioButton::toggled, this, &InitialBinarySetupDialog::refreshSummary);
    connect(m_galleryDlCheck, &QCheckBox::toggled, this, &InitialBinarySetupDialog::refreshSummary);
    connect(m_aria2cCheck, &QCheckBox::toggled, this, &InitialBinarySetupDialog::refreshSummary);
    refreshSummary();
}

bool InitialBinarySetupDialog::preferAppManagedBinaries() const
{
    return m_appManagedFirstButton && m_appManagedFirstButton->isChecked();
}

QStringList InitialBinarySetupDialog::binariesToInstall() const
{
    QStringList binaries;
    const bool provisionAll = preferAppManagedBinaries();
    for (const QString &binary : kRequiredBinaries) {
        if (provisionAll || !binaryIsResolved(binary)) {
            binaries.append(binary);
        }
    }
    if (m_galleryDlCheck->isChecked() && (provisionAll || !binaryIsResolved(QStringLiteral("gallery-dl")))) {
        binaries.append(QStringLiteral("gallery-dl"));
    }
    if (m_aria2cCheck->isChecked() && (provisionAll || !binaryIsResolved(QStringLiteral("aria2c")))) {
        binaries.append(QStringLiteral("aria2c"));
    }
    return binaries;
}

bool InitialBinarySetupDialog::hasSystemBinary() const
{
    for (const QString &binary : kAllBinaries) {
        const QString path = m_configManager->get(
            QStringLiteral("Binaries"), binary + QStringLiteral("_path")).toString();
        if (QFileInfo::exists(path) && !isAppManagedPath(path, m_configManager)) {
            return true;
        }
    }
    return false;
}

bool InitialBinarySetupDialog::binaryIsResolved(const QString &binaryName) const
{
    const QString path = m_configManager->get(
        QStringLiteral("Binaries"), binaryName + QStringLiteral("_path")).toString();
    return QFileInfo::exists(path);
}

void InitialBinarySetupDialog::refreshSummary()
{
    auto *summary = findChild<QLabel *>(QStringLiteral("binarySetupSummary"));
    if (!summary) {
        return;
    }
    const QStringList selected = binariesToInstall();
    summary->setText(selected.isEmpty()
        ? tr("All required tools are already available. LzyDownloader can continue immediately.")
        : tr("Selected tools will be installed one at a time with visible progress: %1.")
              .arg(selected.join(QStringLiteral(", "))));
}
