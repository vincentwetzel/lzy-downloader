#include "AuthenticationPage.h"
#include "core/ConfigManager.h"
#include "utils/BrowserUtils.h"
#include "core/ProcessUtils.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QMessageBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>

AuthenticationPage::AuthenticationPage(ConfigManager *configManager, QWidget *parent)
    : QWidget(parent), m_configManager(configManager) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *authGroup = new QGroupBox(tr("Authentication Access"), this);
    authGroup->setToolTip(tr("Settings for accessing content that requires login, using browser cookies."));
    QFormLayout *authLayout = new QFormLayout(authGroup);

    QStringList installedBrowsers = BrowserUtils::getInstalledBrowsers();
    QStringList orderedBrowsers;

    QString firefoxName, chromeName;
    QMutableStringListIterator it(installedBrowsers);
    while (it.hasNext()) {
        QString browser = it.next();
        if (browser.compare(QStringLiteral("Firefox"), Qt::CaseInsensitive) == 0) { firefoxName = browser; it.remove(); }
        else if (browser.compare(QStringLiteral("Chrome"), Qt::CaseInsensitive) == 0) { chromeName = browser; it.remove(); }
    }
    if (!firefoxName.isEmpty()) orderedBrowsers.append(firefoxName);
    if (!chromeName.isEmpty()) orderedBrowsers.append(chromeName);

    std::sort(installedBrowsers.begin(), installedBrowsers.end(), [](const QString &s1, const QString &s2){
        return s1.compare(s2, Qt::CaseInsensitive) < 0;
    });
    orderedBrowsers.append(installedBrowsers);
    orderedBrowsers.append(tr("None"));

    m_cookiesBrowserCombo = new QComboBox(this);
    m_cookiesBrowserCombo->setToolTip(tr("Choose a web browser to get your login cookies from."));
    m_cookiesBrowserCombo->addItems(orderedBrowsers);
    QLabel *cookiesLabel = new QLabel(tr("Cookies from browser:"), this);
    cookiesLabel->setToolTip(m_cookiesBrowserCombo->toolTip());
    authLayout->addRow(cookiesLabel, m_cookiesBrowserCombo);

    layout->addWidget(authGroup);
    layout->addStretch();

    m_cookieCheckProcess = new QProcess(this);
    connect(m_cookieCheckProcess, &QProcess::started, this, &AuthenticationPage::onCookieCheckProcessStarted);
    connect(m_cookieCheckProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &AuthenticationPage::onCookieCheckProcessFinished);
    connect(m_cookieCheckProcess, &QProcess::errorOccurred, this, &AuthenticationPage::onCookieCheckProcessErrorOccurred);
    connect(m_cookieCheckProcess, &QProcess::readyReadStandardOutput, this, &AuthenticationPage::onCookieCheckProcessReadyReadStandardOutput);
    connect(m_cookieCheckProcess, &QProcess::readyReadStandardError, this, &AuthenticationPage::onCookieCheckProcessReadyReadStandardError);

    m_cookieCheckTimeoutTimer = new QTimer(this);
    m_cookieCheckTimeoutTimer->setSingleShot(true);
    connect(m_cookieCheckTimeoutTimer, &QTimer::timeout, this, &AuthenticationPage::onCookieCheckTimeout);

    connect(m_cookiesBrowserCombo, &QComboBox::currentTextChanged, this, &AuthenticationPage::onCookiesBrowserChanged);
    connect(m_configManager, &ConfigManager::settingChanged, this, &AuthenticationPage::handleConfigSettingChanged);
}

AuthenticationPage::~AuthenticationPage() {
    if (m_cookieCheckProcess->state() != QProcess::NotRunning) {
        ProcessUtils::terminateProcessTree(m_cookieCheckProcess);
        m_cookieCheckProcess->kill();
        m_cookieCheckProcess->waitForFinished(1000);
    }
}

void AuthenticationPage::loadSettings() {
    QSignalBlocker b(m_cookiesBrowserCombo);
    m_lastSavedBrowser = m_configManager->get(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), tr("None")).toString();
    if (m_lastSavedBrowser == tr("None") || m_lastSavedBrowser == QStringLiteral("None")) {
        m_lastSavedBrowser = m_configManager->get(QStringLiteral("General"), QStringLiteral("gallery_cookies_from_browser"), tr("None")).toString();
    }
    m_cookiesBrowserCombo->setCurrentText(m_lastSavedBrowser);
}

void AuthenticationPage::onCookiesBrowserChanged(const QString &text) {
    if (text == m_lastSavedBrowser) return;
    if (text.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0 || text.compare(tr("None"), Qt::CaseInsensitive) == 0) {
        m_lastSavedBrowser = text;
        m_configManager->set(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), text);
        m_configManager->set(QStringLiteral("General"), QStringLiteral("gallery_cookies_from_browser"), text);
        return;
    }
    if (m_cookieCheckProcess->state() != QProcess::NotRunning) {
        m_cookieCheckProcess->setProperty("timedOut", true); // Re-use timedOut property to suppress popups on manual cancellation
        ProcessUtils::terminateProcessTree(m_cookieCheckProcess);
        m_cookieCheckProcess->waitForFinished(2000);
    }
    m_cookieCheckTimeoutTimer->stop();
    setCursor(Qt::WaitCursor);
    m_cookiesBrowserCombo->setEnabled(false);

    // Clear the timeout flag so previous timeouts don't suppress the next valid finish
    m_cookieCheckProcess->setProperty("timedOut", false);
    m_cookieCheckProcess->setProperty("stderr_buffer", QByteArray());

    QStringList args;
    args << QStringLiteral("--cookies-from-browser") << text.toLower() << QStringLiteral("--simulate") << QStringLiteral("--verbose") << QStringLiteral("https://www.youtube.com/watch?v=7x52ID-2H0E");

    ProcessUtils::FoundBinary denoBinary = ProcessUtils::findBinary(QStringLiteral("deno"), m_configManager);
    if (denoBinary.source != QStringLiteral("Not Found")) {
        args << QStringLiteral("--js-runtimes") << QStringLiteral("deno:%1").arg(denoBinary.path);
    }

    ProcessUtils::setProcessEnvironment(*m_cookieCheckProcess);
    m_cookieCheckProcess->start(ProcessUtils::findBinary(QStringLiteral("yt-dlp"), m_configManager).path, args);
}

void AuthenticationPage::onCookieCheckProcessStarted() { m_cookieCheckTimeoutTimer->start(30000); }
void AuthenticationPage::onCookieCheckProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_cookieCheckTimeoutTimer->stop();

    if (m_cookieCheckProcess->property("timedOut").toBool()) {
        return;
    }

    unsetCursor();
    m_cookiesBrowserCombo->setEnabled(true);
    QString selectedBrowser = m_cookiesBrowserCombo->currentText();

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        QString stderrOutput = m_cookieCheckProcess->property(QStringLiteral("stderr_buffer").toLatin1()).toString();
        if (stderrOutput.isEmpty()) {
            stderrOutput = m_cookieCheckProcess->readAllStandardError();
        }
        QString errorMessage = (stderrOutput.contains(QStringLiteral("Unable to get cookie info"), Qt::CaseInsensitive) || stderrOutput.contains(QStringLiteral("database is locked"), Qt::CaseInsensitive))
            ? tr("Failed to access cookies for %1. The browser may be running, which can lock the cookie database. Please close %1 and try again.").arg(selectedBrowser)
            : tr("An unexpected error occurred while checking cookies for %1.\n\nDetails:\n%2").arg(selectedBrowser, stderrOutput);
        QMessageBox::warning(this, tr("Cookie Access Failed"), errorMessage);
        loadSettings(); // Reset to last saved implicitly
    } else {
        m_lastSavedBrowser = selectedBrowser;
        m_configManager->set(QStringLiteral("General"), QStringLiteral("cookies_from_browser"), selectedBrowser);
        m_configManager->set(QStringLiteral("General"), QStringLiteral("gallery_cookies_from_browser"), selectedBrowser);
    }
}
void AuthenticationPage::onCookieCheckProcessErrorOccurred(QProcess::ProcessError error) {
    m_cookieCheckTimeoutTimer->stop();

    if (m_cookieCheckProcess->property("timedOut").toBool()) {
        return;
    }

    // FailedToStart means it never started; we must handle it to unlock the UI.
    if (error != QProcess::FailedToStart && m_cookieCheckProcess->state() == QProcess::NotRunning) {
        return;
    }

    unsetCursor(); m_cookiesBrowserCombo->setEnabled(true);
    QMessageBox::critical(this, tr("Process Error"), error == QProcess::FailedToStart ? tr("Failed to start yt-dlp. Please ensure it is installed and configured in Advanced Settings.") : tr("An unknown error occurred: %1").arg(m_cookieCheckProcess->errorString()));
    loadSettings();
}
void AuthenticationPage::onCookieCheckProcessReadyReadStandardOutput() { qDebug().noquote() << "yt-dlp stdout:" << m_cookieCheckProcess->readAllStandardOutput(); }
void AuthenticationPage::onCookieCheckProcessReadyReadStandardError() { 
    QByteArray data = m_cookieCheckProcess->readAllStandardError();
    QByteArray existing = m_cookieCheckProcess->property("stderr_buffer").toByteArray();
    existing.append(data);
    m_cookieCheckProcess->setProperty("stderr_buffer", existing);
    qWarning().noquote() << "yt-dlp stderr:" << data; 
}
void AuthenticationPage::onCookieCheckTimeout() {
    m_cookieCheckProcess->setProperty("timedOut", true);
    ProcessUtils::terminateProcessTree(m_cookieCheckProcess);
    unsetCursor();
    m_cookiesBrowserCombo->setEnabled(true);
    QMessageBox::warning(this, tr("Timed Out"), tr("The cookie check took too long to respond."));
    loadSettings();
}
void AuthenticationPage::handleConfigSettingChanged(const QString &section, const QString &key, const QVariant &value) {
    if (section == QStringLiteral("General") && (key == QStringLiteral("cookies_from_browser") || key == QStringLiteral("gallery_cookies_from_browser"))) {
        m_lastSavedBrowser = value.toString();
        if (m_cookiesBrowserCombo->currentText() != m_lastSavedBrowser) {
            QSignalBlocker b(m_cookiesBrowserCombo);
            m_cookiesBrowserCombo->setCurrentText(m_lastSavedBrowser);
        }
    }
}