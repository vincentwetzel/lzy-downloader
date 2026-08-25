#include "YtDlpWorker.h"

#include "core/DownloadTempCleanup.h"

#include <QDebug>
#include <QRegularExpression>
#include <QTimer>

namespace {

const QRegularExpression &fatalDownloadRegex()
{
    static const QRegularExpression regex(
        QStringLiteral("this video is unavailable|"
                       "video unavailable|"
                       "video is private|"
                       "private video|"
                       "this video has been removed|"
                       "this content is no longer available|"
                       "requested tweet is unavailable|"
                       "requested content was removed|"
                       "suspended|"
                       "violating youtube's terms of service|"
                       "not available in your country|"
                       "no video formats found|"
                       "sign in to confirm|"
                       "did not get any data blocks|"
                       "fragment not found|"
                       "error reading header|"
                       "invalid data found when processing input|"
                       "error opening input files|"
                       "no space left on device|"
                       "not enough space|"
                       "disk full|"
                       "error code:\\s*-28|"
                       "errno\\s*28"),
        QRegularExpression::CaseInsensitiveOption);
    return regex;
}

template <typename Predicate>
bool anyDiagnosticMatches(const QStringList &lines, Predicate predicate)
{
    for (const QString &line : lines) {
        if (predicate(line)) {
            return true;
        }
    }
    return false;
}

bool isMissingExternalDownloaderOutputDiagnostic(const QString &diagnostic)
{
    if (!diagnostic.contains(QStringLiteral("Unable to download video"), Qt::CaseInsensitive)
        || !diagnostic.contains(QStringLiteral(".part"), Qt::CaseInsensitive)) {
        return false;
    }

    const bool fileNotFound = diagnostic.contains(QStringLiteral("FileNotFoundError"), Qt::CaseInsensitive)
                              || diagnostic.contains(QStringLiteral("[WinError 2]"), Qt::CaseInsensitive)
                              || diagnostic.contains(QStringLiteral("[WinError 3]"), Qt::CaseInsensitive);
    const bool missingPath = diagnostic.contains(QStringLiteral("cannot find the path specified"), Qt::CaseInsensitive)
                             || diagnostic.contains(QStringLiteral("cannot find the file specified"), Qt::CaseInsensitive)
                             || diagnostic.contains(QStringLiteral("no such file or directory"), Qt::CaseInsensitive);
    return fileNotFound && missingPath;
}

void removeArgumentAndValue(QStringList &arguments, const QString &flag)
{
    qsizetype index = arguments.indexOf(flag);
    while (index >= 0) {
        arguments.removeAt(index);
        if (index < arguments.size()) {
            arguments.removeAt(index);
        }
        index = arguments.indexOf(flag);
    }
}

}

bool YtDlpWorker::isIncompleteMediaDiagnosticLine(const QString &line) const
{
    if (line.isEmpty()) {
        return false;
    }

    if (line.contains(QStringLiteral("did not get any data blocks"), Qt::CaseInsensitive)) {
        return line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
               line.startsWith(QStringLiteral("[download]"), Qt::CaseInsensitive);
    }
    if (line.contains(QStringLiteral("fragment not found"), Qt::CaseInsensitive)) {
        return line.startsWith(QStringLiteral("[download]"), Qt::CaseInsensitive) ||
               line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive);
    }
    if (line.contains(QStringLiteral("error reading header"), Qt::CaseInsensitive)) {
        return line.startsWith(QStringLiteral("[in#"), Qt::CaseInsensitive) ||
               line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
               line.contains(QStringLiteral("ffmpeg"), Qt::CaseInsensitive);
    }
    if (line.contains(QStringLiteral("invalid data found when processing input"), Qt::CaseInsensitive)) {
        return line.startsWith(QStringLiteral("[in#"), Qt::CaseInsensitive) ||
               line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
               line.contains(QStringLiteral("error opening input"), Qt::CaseInsensitive);
    }
    return false;
}

bool YtDlpWorker::hasFatalDownloadDiagnostic() const
{
    const auto matches = [](const QString &line) {
        return fatalDownloadRegex().match(line).hasMatch();
    };
    return anyDiagnosticMatches(m_errorLines, matches) || anyDiagnosticMatches(m_allOutputLines, [this](const QString &line) {
        return isIncompleteMediaDiagnosticLine(line);
    });
}

bool YtDlpWorker::hasIncompleteMediaDiagnostic() const
{
    const auto matches = [this](const QString &line) {
        return isIncompleteMediaDiagnosticLine(line);
    };
    return anyDiagnosticMatches(m_errorLines, matches) || anyDiagnosticMatches(m_allOutputLines, matches);
}

bool YtDlpWorker::retryWithoutAria2cIfTransientFailure(const QString &diagnostic)
{
    if (m_retriedWithoutAria2c || !m_args.contains(QStringLiteral("--external-downloader"))) {
        return false;
    }

    static const QRegularExpression transientAria2Error(
        QStringLiteral("aria2c exited with code\\s+(?:2|5|6|29)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch errorMatch = transientAria2Error.match(diagnostic);
    const bool missingOutput = isMissingExternalDownloaderOutputDiagnostic(diagnostic);
    if (!errorMatch.hasMatch() && !missingOutput) {
        return false;
    }

    m_retriedWithoutAria2c = true;
    m_recoveryDiagnostic = missingOutput
        ? tr("aria2c returned without the expected temporary media file before native fallback.")
        : tr("aria2c exited with transient error code %1 before native fallback.")
              .arg(errorMatch.captured(0).section(QLatin1Char(' '), -1));
    removeArgumentAndValue(m_args, QStringLiteral("--external-downloader"));
    removeArgumentAndValue(m_args, QStringLiteral("--external-downloader-args"));

    cleanupMetadataSidecarsForRetry();
    if (m_process) {
        m_process->setProperty("accumulated_stderr", QString());
    }

    qWarning() << "[YtDlpWorker] Recoverable aria2c output failure detected for" << m_id
               << "; retrying once with yt-dlp's native downloader.";

    QVariantMap progressData;
    progressData.insert(QStringLiteral("status"),
                        missingOutput
                            ? tr("aria2c did not leave its expected media output; retrying with the native downloader...")
                            : tr("aria2c encountered a temporary server or network error; retrying with the native downloader..."));
    progressData.insert(QStringLiteral("progress"), -1);
    emit progressUpdated(m_id, progressData);

    QTimer::singleShot(RECOVERY_RETRY_DELAY_MS, this, [this]() {
        if (!m_finishEmitted) {
            // A short second pass handles Windows scanners or child-process
            // teardown that still held the metadata sidecar during the first
            // cleanup attempt. Media partials are deliberately preserved.
            cleanupMetadataSidecarsForRetry();
            start();
        }
    });
    return true;
}
