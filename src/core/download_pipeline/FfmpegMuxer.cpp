#include "FfmpegMuxer.h"
#include "core/ProcessUtils.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>

FfmpegMuxer::FfmpegMuxer(QObject *parent)
    : QObject(parent), m_process(new QProcess(this))
{
    ProcessUtils::setProcessEnvironment(*m_process);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        appendProcessOutput(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        appendProcessOutput(m_process->readAllStandardError());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit mergeFailed(tr("Failed to start ffmpeg. Is the executable missing?"));
        }
    });

    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        appendProcessOutput(m_process->readAllStandardOutput());
        appendProcessOutput(m_process->readAllStandardError());
        if (exitStatus == QProcess::CrashExit || exitCode != 0) {
            QString errorMsg = tr("FFmpeg process failed. Error: %1\n%2").arg(m_process->errorString(), m_processOutputTail);
            emit mergeFailed(errorMsg);
            return;
        }

        // Cleanup the original unmerged parts now that the merge is successful
        for (const QString &inputFile : m_currentInputFiles) {
            if (QFile::exists(inputFile)) {
                QFile::remove(inputFile);
            }
        }
        // Also clean up subtitle parts
        for (const SubtitleFile &subFile : m_currentSubtitleFiles) {
            if (QFile::exists(subFile.path)) {
                QFile::remove(subFile.path);
            }
        }

        emit mergeSuccess(m_currentOutputFile);
    });
}

void FfmpegMuxer::merge(const QString &ffmpegPath, const QStringList &inputFiles, const QString &outputFile, const QString &title, const QString &artworkPath, const QList<SubtitleFile> &subtitleFiles)
{
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
    const bool supportsSubtitles = (ext == QStringLiteral("mp4") || ext == QStringLiteral("mkv") || ext == QStringLiteral("webm") || ext == QStringLiteral("mov") || ext == QStringLiteral("m4v") || ext == QStringLiteral("m4a"));
    const bool hasSubtitles = !subtitleFiles.isEmpty() && supportsSubtitles;
    if (!subtitleFiles.isEmpty() && !supportsSubtitles) {
        qDebug() << "Output container does not support embedded subtitles. Ignoring.";
    }

    // If there is only one file, and no metadata/artwork/subtitles, no merging is needed
    if (inputFiles.size() == 1 && title.isEmpty() && !hasArtwork && !hasSubtitles) {
        if (QFile::exists(outputFile)) QFile::remove(outputFile);
        if (QFile::rename(inputFiles.first(), outputFile)) {
            for (const SubtitleFile &subFile : m_currentSubtitleFiles) {
                if (QFile::exists(subFile.path)) {
                    QFile::remove(subFile.path);
                }
            }
            emit mergeSuccess(outputFile);
        } else {
            emit mergeFailed(tr("Failed to rename single downloaded file to final output."));
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
    int streamIndex = 0;
    for (int i = 0; i < inputFiles.size(); ++i, ++streamIndex) {
        args << QStringLiteral("-map") << QString::number(i);
    }

    // Map subtitle inputs
    if (hasSubtitles) {
        for (int i = 0; i < subtitleFiles.size(); ++i, ++streamIndex) {
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

        for (int i = 0; i < subtitleFiles.size(); ++i) {
            args << QStringLiteral("-metadata:s:s:%1").arg(i) << QStringLiteral("language=%1").arg(subtitleFiles[i].language);
        }
    }

    if (hasArtwork) {
        // If audio-only, artwork is the first video stream (v:0). If video+audio, it's the second (v:1).
        bool isAudioOnly = (ext == QStringLiteral("mp3") || ext == QStringLiteral("m4a") || ext == QStringLiteral("wav") || ext == QStringLiteral("flac") || ext == QStringLiteral("opus") || ext == QStringLiteral("ogg") || ext == QStringLiteral("aac"));
        args << QStringLiteral("-disposition:v:%1").arg(isAudioOnly ? 0 : 1) << QStringLiteral("attached_pic");
    }

    if (!title.isEmpty()) {
        args << QStringLiteral("-metadata") << QStringLiteral("title=%1").arg(title);
    }

    args << QStringLiteral("-y") << outputFile;

    qInfo() << "[FfmpegMuxer] Starting ffmpeg merge for" << outputFile;
    m_process->start(ffmpegPath, args);
}

void FfmpegMuxer::appendProcessOutput(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

    m_processOutputTail += QString::fromUtf8(data);
    constexpr qsizetype maxTailLength = 12000;
    if (m_processOutputTail.size() > maxTailLength) {
        m_processOutputTail = m_processOutputTail.right(maxTailLength);
    }
}

void FfmpegMuxer::cancel() {
    if (m_process->state() != QProcess::NotRunning) {
        qInfo() << "[FfmpegMuxer] Cancelling ffmpeg merge for" << m_currentOutputFile;
        ProcessUtils::terminateProcessTree(m_process);
    }
}
