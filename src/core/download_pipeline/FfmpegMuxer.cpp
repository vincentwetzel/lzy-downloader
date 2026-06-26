#include "FfmpegMuxer.h"
#include "core/ProcessUtils.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>

namespace {
    /**
     * @brief Helper for transient file locks (Windows virus scanners, indexers, etc.)
     * 
     * Performs a safe, bounded retry to guarantee deletion of temporary and source files.
     * 
     * @param filePath Absolute path of the file to remove.
     * @param description Human-readable description of the file type for diagnostics.
     * @param retries Number of retry attempts remaining.
     */
    void safeRemoveFile(const QString &filePath, const QString &description, int retries = 3) {
        if (filePath.isEmpty()) {
            return;
        }
        if (QFile::remove(filePath)) {
            qDebug() << "[FfmpegMuxer] Cleaned up" << description << "file:" << filePath;
        } else if (QFile::exists(filePath)) {
            qWarning() << "[FfmpegMuxer] Failed to clean up" << description << "file:" << filePath << "on first attempt.";
            if (retries > 0) {
                QTimer::singleShot(100, QCoreApplication::instance(), [filePath, description, retries]() {
                    safeRemoveFile(filePath, description, retries - 1);
                });
            } else {
                qCritical() << "[FfmpegMuxer] Failed to clean up" << description << "file:" << filePath << "after bounded retries.";
            }
        }
    }

    /**
     * @brief Checks if the container format specified by its extension supports embedded subtitles.
     * 
     * Uses allocation-free comparison to avoid static initialization/destruction overhead.
     * 
     * @param ext Lowercase file extension to inspect.
     */
    bool supportsSubtitles(const QString &ext) {
        return ext == QLatin1String("mp4") ||
               ext == QLatin1String("mkv") ||
               ext == QLatin1String("webm") ||
               ext == QLatin1String("mov") ||
               ext == QLatin1String("m4v") ||
               ext == QLatin1String("m4a");
    }

    /**
     * @brief Checks if the extension represents an audio-only container format.
     * 
     * Uses allocation-free comparison to avoid static initialization/destruction overhead.
     * Includes Matroska Audio (.mka) support.
     * 
     * @param ext Lowercase file extension to inspect.
     */
    bool isAudioOnly(const QString &ext) {
        return ext == QLatin1String("mp3") ||
               ext == QLatin1String("m4a") ||
               ext == QLatin1String("mka") ||
               ext == QLatin1String("wav") ||
               ext == QLatin1String("flac") ||
               ext == QLatin1String("opus") ||
               ext == QLatin1String("ogg") ||
               ext == QLatin1String("aac");
    }
}

/**
 * @brief Constructs the FfmpegMuxer instance and initializes standard I/O connections.
 * 
 * Connects process output and finished signals to asynchronous stream parsers with 
 * standard lifetime-bounded lambda context handlers to prevent use-after-free conditions.
 * 
 * @param parent Optional parent QObject for automatic tree-lifetime management.
 */
FfmpegMuxer::FfmpegMuxer(QObject *parent)
    : QObject(parent), m_process(new QProcess(this))
{
    if (!m_process) {
        qCritical() << "[FfmpegMuxer] Failed to allocate m_process!";
        return;
    }

    ProcessUtils::setProcessEnvironment(*m_process);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process) {
            return;
        }
        appendProcessOutput(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_process) {
            return;
        }
        appendProcessOutput(m_process->readAllStandardError());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit mergeFailed(tr("Failed to start ffmpeg. Is the executable missing?"));
        }
    });

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!m_process) {
            return;
        }
        appendProcessOutput(m_process->readAllStandardOutput());
        appendProcessOutput(m_process->readAllStandardError());

        QByteArray remaining = m_process->property("rawOutputBuffer").toByteArray();
        if (!remaining.isEmpty()) {
            m_processOutputTail += QString::fromUtf8(remaining);
            m_process->setProperty("rawOutputBuffer", QByteArray());
        }

        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            QString errorMsg = tr("FFmpeg process failed. Error: %1\n%2").arg(m_process->errorString(), m_processOutputTail);
            emit mergeFailed(errorMsg);
            return;
        }

        // Cleanup the original unmerged parts now that the merge is successful
        for (const QString &inputFile : m_currentInputFiles) {
            safeRemoveFile(inputFile, QStringLiteral("original unmerged fragment"));
        }
        // Also clean up subtitle parts
        for (const SubtitleFile &subFile : m_currentSubtitleFiles) {
            safeRemoveFile(subFile.path, QStringLiteral("subtitle fragment"));
        }

        emit mergeSuccess(m_currentOutputFile);
    });
}

/**
 * @brief Merges multiple input media and subtitle files into a unified output container using FFmpeg.
 * 
 * If there is only one input file with no metadata, subtitles, or artwork, it optimizes the operation
 * by bypassing the FFmpeg process entirely and using a fast, atomic file system rename/copy fallback.
 * 
 * @param ffmpegPath Absolute path to the FFmpeg executable.
 * @param inputFiles List of absolute paths of input files (e.g., separate video and audio streams).
 * @param outputFile Absolute path where the final merged media file should be created.
 * @param title Optional metadata title to embed in the output container.
 * @param artworkPath Optional absolute path of cover artwork image to embed.
 * @param subtitleFiles List of subtitle files to embed with their respective language tags.
 * @pre m_process must not be null and must be in QProcess::NotRunning state.
 */
void FfmpegMuxer::merge(const QString &ffmpegPath, const QStringList &inputFiles, const QString &outputFile, const QString &title, const QString &artworkPath, const QList<SubtitleFile> &subtitleFiles)
{
    if (!m_process) {
        emit mergeFailed(tr("Internal process error: QProcess instance is null."));
        return;
    }

    if (m_process->state() != QProcess::NotRunning) {
        emit mergeFailed(tr("FFmpeg is already running another job."));
        return;
    }

    if (inputFiles.isEmpty()) {
        emit mergeFailed(tr("No input files provided for merging."));
        return;
    }

    m_currentInputFiles = inputFiles;
    m_currentSubtitleFiles = subtitleFiles;
    m_currentOutputFile = outputFile;
    m_processOutputTail.clear();

    const bool hasArtwork = !artworkPath.isEmpty() && QFile::exists(artworkPath);
    if (hasArtwork) {
        m_currentInputFiles.append(artworkPath); // Append to list so it gets auto-deleted on cleanup!
    }

    const QString ext = QFileInfo(outputFile).suffix().toLower();
        const bool extSupportsSubtitles = supportsSubtitles(ext);
        const bool hasSubtitles = !subtitleFiles.isEmpty() && extSupportsSubtitles;
        if (!subtitleFiles.isEmpty() && !extSupportsSubtitles) {
        qDebug() << "Output container does not support embedded subtitles. Ignoring.";
    }

    // If there is only one file, and no metadata/artwork/subtitles, no merging is needed
    if (inputFiles.size() == 1 && title.isEmpty() && !hasArtwork && !hasSubtitles) {
        if (QFile::exists(outputFile)) {
            safeRemoveFile(outputFile, QStringLiteral("pre-existing output"));
        }

        QFile sourceFile(inputFiles.first());
        bool moveSuccess = false;
        QString errorStr;

        if (sourceFile.rename(outputFile)) {
            moveSuccess = true;
        } else {
            errorStr = sourceFile.errorString();
            qWarning() << "[FfmpegMuxer] Rename failed, trying copy fallback. Error:" << errorStr;
            if (sourceFile.copy(outputFile)) {
                safeRemoveFile(inputFiles.first(), QStringLiteral("source after copy fallback"));
                moveSuccess = true;
            } else {
                errorStr = sourceFile.errorString();
                qCritical() << "[FfmpegMuxer] Copy fallback failed. Error:" << errorStr;
            }
        }

        if (moveSuccess) {
            for (const SubtitleFile &subFile : m_currentSubtitleFiles) {
                safeRemoveFile(subFile.path, QStringLiteral("redundant subtitle"));
            }
            emit mergeSuccess(outputFile);
        } else {
            emit mergeFailed(tr("Failed to move single downloaded file to final destination: %1").arg(errorStr));
        }
        return;
    }

    QStringList args;
    args << QStringLiteral("-nostdin");

    for (const QString &inputFile : inputFiles) {
        args << QStringLiteral("-i") << inputFile;
    }

    if (hasSubtitles) {
        for (const SubtitleFile &subFile : subtitleFiles) {
            args << QStringLiteral("-i") << subFile.path;
        }
    }

    if (hasArtwork) {
        args << QStringLiteral("-i") << artworkPath;
    }

    // Map all media inputs (Video/Audio)
    qsizetype streamIndex = 0;
    for (qsizetype i = 0; i < inputFiles.size(); ++i, ++streamIndex) {
        args << QStringLiteral("-map") << QString::number(i);
    }

    // Map subtitle inputs
    if (hasSubtitles) {
        for (qsizetype i = 0; i < subtitleFiles.size(); ++i, ++streamIndex) {
            args << QStringLiteral("-map") << QString::number(streamIndex);
        }
    }

    // Map the artwork as an attached picture stream
    if (hasArtwork) {
        args << QStringLiteral("-map") << QString::number(streamIndex);
    }

    // Copy media streams without re-encoding
    args << QStringLiteral("-c:v") << QStringLiteral("copy") << QStringLiteral("-c:a") << QStringLiteral("copy");

    if (hasSubtitles) {
        // MP4 requires mov_text, MKV/WebM handles SRT best
        if (outputFile.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive) || outputFile.endsWith(QStringLiteral(".m4a"), Qt::CaseInsensitive)) {
            args << QStringLiteral("-c:s") << QStringLiteral("mov_text");
        } else if (outputFile.endsWith(QStringLiteral(".webm"), Qt::CaseInsensitive)) {
            args << QStringLiteral("-c:s") << QStringLiteral("webvtt");
        } else {
            args << QStringLiteral("-c:s") << QStringLiteral("srt");
        }

        for (qsizetype i = 0; i < subtitleFiles.size(); ++i) {
            args << QStringLiteral("-metadata:s:s:%1").arg(i) << QStringLiteral("language=%1").arg(subtitleFiles[i].language);
        }
    }

    if (hasArtwork) {
        // If audio-only, artwork is the first video stream (v:0). If video+audio, it's the second (v:1).
            args << QStringLiteral("-disposition:v:%1").arg(isAudioOnly(ext) ? 0 : 1) << QStringLiteral("attached_pic");
    }

    if (!title.isEmpty()) {
        args << QStringLiteral("-metadata") << QStringLiteral("title=%1").arg(title);
    }

    args << QStringLiteral("-y") << outputFile;

    qInfo() << "[FfmpegMuxer] Starting ffmpeg merge for" << outputFile;
    m_process->start(ffmpegPath, args);
}

/**
 * @brief Appends raw standard output/error chunks from the FFmpeg process into the diagnostic log tail.
 * 
 * Decodes complete UTF-8 lines using safe buffered byte-level lookup to prevent split multi-byte character corruption,
 * and keeps a rolling memory-bounded tail to prevent linear heap memory consumption.
 * 
 * @param data Raw byte array read from standard output or standard error.
 * @pre m_process must be valid.
 */
void FfmpegMuxer::appendProcessOutput(const QByteArray &data)
{
    if (data.isEmpty() || !m_process) {
        return;
    }

    QByteArray buffer = m_process->property("rawOutputBuffer").toByteArray();
    buffer.append(data);

    qsizetype lastDelimiter = qMax(buffer.lastIndexOf('\n'), buffer.lastIndexOf('\r'));
    if (lastDelimiter != -1) {
        m_processOutputTail += QString::fromUtf8(buffer.constData(), lastDelimiter + 1);
        buffer.remove(0, lastDelimiter + 1);
    }
    m_process->setProperty("rawOutputBuffer", buffer);

    constexpr qsizetype maxTailLength = 12000;
    if (m_processOutputTail.size() > maxTailLength) {
        m_processOutputTail = m_processOutputTail.right(maxTailLength);
    }
}

/**
 * @brief Forcefully terminates the active FFmpeg process tree if it is currently running.
 * 
 * Ensures no orphaned child processes survive after the application exits or is cancelled.
 * @pre m_process must be valid.
 */
void FfmpegMuxer::cancel() {
    if (!m_process) {
        return;
    }
    if (m_process->state() != QProcess::NotRunning) {
        qInfo() << "[FfmpegMuxer] Cancelling ffmpeg merge for" << m_currentOutputFile;
        ProcessUtils::terminateProcessTree(m_process);
        m_process->kill(); // Forcefully kill the QProcess instance as fallback
    }
}
