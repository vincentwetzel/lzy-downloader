#include "StartTabCommandPreviewUpdater.h"
#include <QDebug>
#include <QDir>
#include <QComboBox>
#include <QTextEdit>

StartTabCommandPreviewUpdater::StartTabCommandPreviewUpdater(ConfigManager *configManager, StartTabUiBuilder *uiBuilder,
                                                             YtDlpArgsBuilder *ytDlpArgsBuilder, GalleryDlArgsBuilder *galleryDlArgsBuilder,
                                                             QObject *parent)
    : QObject(parent),
      m_configManager(configManager),
      m_uiBuilder(uiBuilder),
      m_ytDlpArgsBuilder(ytDlpArgsBuilder),
      m_galleryDlArgsBuilder(galleryDlArgsBuilder)
{
    if (!m_uiBuilder) {
        qCritical() << "CRITICAL ERROR: m_uiBuilder is null in StartTabCommandPreviewUpdater constructor!";
        return;
    }

    // Connect UI element changes to updateCommandPreview
    if (m_uiBuilder->urlInput()) connect(m_uiBuilder->urlInput(), &QTextEdit::textChanged, this, &StartTabCommandPreviewUpdater::updateCommandPreview);
    if (m_uiBuilder->downloadTypeCombo()) connect(m_uiBuilder->downloadTypeCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StartTabCommandPreviewUpdater::updateCommandPreview);
    if (m_uiBuilder->playlistLogicCombo()) connect(m_uiBuilder->playlistLogicCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StartTabCommandPreviewUpdater::updateCommandPreview);
    if (m_uiBuilder->maxConcurrentCombo()) connect(m_uiBuilder->maxConcurrentCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StartTabCommandPreviewUpdater::updateCommandPreview);
    if (m_uiBuilder->rateLimitCombo()) connect(m_uiBuilder->rateLimitCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StartTabCommandPreviewUpdater::updateCommandPreview);
    if (m_uiBuilder->overrideDuplicateCheck()) connect(m_uiBuilder->overrideDuplicateCheck(), &ToggleSwitch::toggled, this, &StartTabCommandPreviewUpdater::updateCommandPreview);
}

StartTabCommandPreviewUpdater::~StartTabCommandPreviewUpdater()
{
    // No owned pointers to delete
}

void StartTabCommandPreviewUpdater::updateCommandPreview()
{
    qDebug() << "StartTabCommandPreviewUpdater::updateCommandPreview called.";

    if (!m_uiBuilder) {
        qCritical() << "CRITICAL ERROR: m_uiBuilder is null in updateCommandPreview!";
        return;
    }

    if (!m_uiBuilder->urlInput() || !m_uiBuilder->overrideDuplicateCheck() || !m_uiBuilder->rateLimitCombo() || !m_uiBuilder->downloadTypeCombo() || !m_uiBuilder->playlistLogicCombo() || !m_uiBuilder->commandPreview()) {
        qCritical() << "CRITICAL ERROR: One or more UI elements are null in updateCommandPreview!";
        return;
    }

    QString url = m_uiBuilder->urlInput()->toPlainText().trimmed().split('\n').first();
    if (url.isEmpty()) {
        url = QStringLiteral("[URL]");
    }

    QVariantMap options;
    options.insert(QStringLiteral("override_archive"), m_uiBuilder->overrideDuplicateCheck()->isChecked());
    options.insert(QStringLiteral("rate_limit"), m_uiBuilder->rateLimitCombo()->currentText());

    QString downloadType = m_uiBuilder->downloadTypeCombo()->currentData().toString();
    if (downloadType == QStringLiteral("gallery")) {
        QString galleryDlPath = resolveExecutablePath(QStringLiteral("gallery-dl.exe"));
        if (galleryDlPath.isEmpty()) {
            galleryDlPath = QStringLiteral("gallery-dl.exe");
        }
        if (!m_galleryDlArgsBuilder) {
            qCritical() << "CRITICAL ERROR: m_galleryDlArgsBuilder is null in updateCommandPreview!";
            return;
        }
        QStringList args = m_galleryDlArgsBuilder->build(url, options);
        QString command = QStringLiteral("%1 %2").arg(QDir::toNativeSeparators(galleryDlPath), args.join(QStringLiteral(" ")));
        m_uiBuilder->commandPreview()->setText(command);
        return;
    }

    options.insert(QStringLiteral("type"), downloadType);
    options.insert(QStringLiteral("playlist_logic"), m_uiBuilder->playlistLogicCombo()->currentText());

    if (!m_ytDlpArgsBuilder) {
        qCritical() << "CRITICAL ERROR: m_ytDlpArgsBuilder is null in updateCommandPreview!";
        return;
    }
    QStringList args = m_ytDlpArgsBuilder->build(m_configManager, url, options);
    QString ytDlpPath = resolveExecutablePath(QStringLiteral("yt-dlp.exe"));
    if (ytDlpPath.isEmpty()) {
        ytDlpPath = QStringLiteral("yt-dlp.exe");
    }
    if (!m_configManager) {
        qCritical() << "CRITICAL ERROR: m_configManager is null in updateCommandPreview!";
        return;
    }
    bool singleLine = m_configManager->get(QStringLiteral("General"), QStringLiteral("single_line_preview"), false).toBool();

    if (singleLine) {
        QString command = QStringLiteral("%1 %2").arg(QDir::toNativeSeparators(ytDlpPath), args.join(QStringLiteral(" ")));
        m_uiBuilder->commandPreview()->setText(command);
    } else {
        QString commandUrl = args.isEmpty() ? QString() : args.takeFirst();

        QStringList formattedArgs;
        for (int i = 0; i < args.size(); ++i) {
            if (args[i].startsWith(QStringLiteral("-")) && i + 1 < args.size() && !args[i+1].startsWith(QStringLiteral("-"))) {
                formattedArgs.append(QStringLiteral("%1 %2").arg(args[i], args[i+1]));
                ++i;
            } else {
                formattedArgs.append(args[i]);
            }
        }

        QString command = QDir::toNativeSeparators(ytDlpPath) + (commandUrl.isEmpty() ? QString() : QStringLiteral(" %1").arg(commandUrl));
        if (!formattedArgs.isEmpty()) {
            command += QStringLiteral(" \\\n    %1").arg(formattedArgs.join(QStringLiteral(" \\\n    ")));
        }
        m_uiBuilder->commandPreview()->setText(command);
    }
    qDebug() << "StartTabCommandPreviewUpdater::updateCommandPreview finished.";
}

QString StartTabCommandPreviewUpdater::resolveExecutablePath(const QString &name) const {
    QString baseName = name;
    if (baseName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        baseName.chop(4);
    }

    const ProcessUtils::FoundBinary binary = ProcessUtils::findBinary(baseName, m_configManager);
    if (binary.source != QStringLiteral("Not Found") && !binary.path.isEmpty()) {
        return binary.path;
    }

    return QString();
}
