#include "YtDlpWorker.h"

#include <QDebug>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

namespace {
constexpr int FFMPEG_TELEMETRY_INTERVAL_MS = 2000;
constexpr int FFMPEG_PROGRESS_LOG_INTERVAL_MS = 5000;

QString progressValue(const QVariantMap &fields, const QString &key)
{
    return fields.value(key).toString();
}

QString formatProcessTree(const ProcessDiagnostics::ProcessResourceSnapshot &snapshot)
{
    QStringList entries;
    for (const ProcessDiagnostics::ProcessResourceDetail &process : snapshot.processes) {
        entries.append(QStringLiteral("%1:%2<- %3 cpu_ms=%4 rss_mb=%5 read_mb=%6 write_mb=%7 start=%8")
                           .arg(process.pid)
                           .arg(process.imageName)
                           .arg(process.parentPid)
                           .arg(process.cpuTimeMs)
                           .arg(QString::number(static_cast<double>(process.residentBytes) / (1024.0 * 1024.0), 'f', 1))
                           .arg(QString::number(static_cast<double>(process.readBytes) / (1024.0 * 1024.0), 'f', 1))
                           .arg(QString::number(static_cast<double>(process.writeBytes) / (1024.0 * 1024.0), 'f', 1))
                           .arg(process.creationTime));
    }
    return entries.join(QLatin1Char(','));
}

QSet<QString> processIdentitySet(const ProcessDiagnostics::ProcessResourceSnapshot &snapshot)
{
    QSet<QString> identities;
    for (const ProcessDiagnostics::ProcessResourceDetail &process : snapshot.processes) {
        identities.insert(QStringLiteral("%1:%2").arg(process.pid).arg(process.creationTime));
    }
    return identities;
}

QString stageNameFromLine(const QString &line)
{
    static const QRegularExpression stageRegex(
        QStringLiteral(R"(^\[([^\]]*(?:Merger|ModifyChapters|SponsorBlock|Metadata|EmbedSubtitle|EmbedThumbnail|Thumbnail)[^\]]*)\])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = stageRegex.match(line);
    return match.hasMatch() ? match.captured(1) : QString();
}

bool parseFfmpegProgressLine(const QString &line, QString *key, QString *value)
{
    const qsizetype separator = line.indexOf(QLatin1Char('='));
    if (separator <= 0 || !key || !value) {
        return false;
    }

    const QString candidateKey = line.left(separator).trimmed();
    if (candidateKey.isEmpty()) {
        return false;
    }
    static const QRegularExpression keyRegex(QStringLiteral("^[a-z_]+$"));
    if (!keyRegex.match(candidateKey).hasMatch()) {
        return false;
    }
    *key = candidateKey;
    *value = line.mid(separator + 1).trimmed();
    return true;
}
} // namespace

void YtDlpWorker::beginFfmpegStage(const QString &stage)
{
    if (stage.isEmpty()) {
        return;
    }
    if (!m_ffmpegStage.isEmpty()) {
        finishFfmpegStage(QStringLiteral("next_stage"));
    }

    m_ffmpegStage = stage;
    m_ffmpegStageTimer.start();
    m_ffmpegTelemetryClock.start();
    m_lastFfmpegTelemetryMs = -1;
    m_lastFfmpegResourceSnapshot = {};
    m_ffmpegProgressFields.clear();
    if (m_ffmpegTelemetryTimer) {
        m_ffmpegTelemetryTimer->start(FFMPEG_TELEMETRY_INTERVAL_MS);
    }

    qInfo() << "[YtDlpWorker][ffmpeg stage] started"
            << "id=" << m_id << "stage=" << stage
            << "parent_pid=" << (m_process ? m_process->processId() : 0);
}

void YtDlpWorker::finishFfmpegStage(const QString &reason)
{
    if (m_ffmpegStage.isEmpty()) {
        return;
    }

    const qint64 elapsedMs = m_ffmpegStageTimer.isValid() ? m_ffmpegStageTimer.elapsed() : -1;
    qInfo() << "[YtDlpWorker][ffmpeg stage] finished"
            << "id=" << m_id << "stage=" << m_ffmpegStage
            << "elapsed_ms=" << elapsedMs << "reason=" << reason
            << "frame=" << progressValue(m_ffmpegProgressFields, QStringLiteral("frame"))
            << "out_time_ms=" << progressValue(m_ffmpegProgressFields, QStringLiteral("out_time_ms"))
            << "speed=" << progressValue(m_ffmpegProgressFields, QStringLiteral("speed"));

    if (m_ffmpegTelemetryTimer) {
        m_ffmpegTelemetryTimer->stop();
    }
    m_ffmpegStage.clear();
    m_ffmpegProgressFields.clear();
    m_lastFfmpegTelemetryMs = -1;
    m_lastFfmpegResourceSnapshot = {};
}

void YtDlpWorker::logFfmpegTelemetry()
{
    if (m_ffmpegStage.isEmpty() || !m_process || m_process->state() == QProcess::NotRunning) {
        return;
    }

    const qint64 nowMs = m_ffmpegTelemetryClock.isValid() ? m_ffmpegTelemetryClock.elapsed() : 0;
    const ProcessDiagnostics::ProcessResourceSnapshot snapshot =
        ProcessDiagnostics::captureProcessTree(m_process->processId());

    const bool processIdentityChanged = m_lastFfmpegResourceSnapshot.available
        && snapshot.rootCreationTime != 0
        && m_lastFfmpegResourceSnapshot.rootCreationTime != 0
        && snapshot.rootCreationTime != m_lastFfmpegResourceSnapshot.rootCreationTime;
    const bool processTreeChanged = m_lastFfmpegResourceSnapshot.available
        && processIdentitySet(snapshot) != processIdentitySet(m_lastFfmpegResourceSnapshot);

    QString cpuPercent = QStringLiteral("unavailable");
    if (!processIdentityChanged && !processTreeChanged && m_lastFfmpegTelemetryMs >= 0
        && snapshot.available && m_lastFfmpegResourceSnapshot.available) {
        const qint64 wallMs = nowMs - m_lastFfmpegTelemetryMs;
        const quint64 cpuDelta = snapshot.cpuTimeMs >= m_lastFfmpegResourceSnapshot.cpuTimeMs
            ? snapshot.cpuTimeMs - m_lastFfmpegResourceSnapshot.cpuTimeMs : 0;
        if (wallMs > 0) {
            cpuPercent = QString::number((100.0 * static_cast<double>(cpuDelta)) / static_cast<double>(wallMs), 'f', 1);
        }
    }

    QString readMbPerSecond = QStringLiteral("unavailable");
    QString writeMbPerSecond = QStringLiteral("unavailable");
    if (!processIdentityChanged && !processTreeChanged && m_lastFfmpegTelemetryMs >= 0
        && snapshot.available && m_lastFfmpegResourceSnapshot.available) {
        const qint64 wallMs = nowMs - m_lastFfmpegTelemetryMs;
        if (wallMs > 0) {
            const quint64 readDelta = snapshot.readBytes >= m_lastFfmpegResourceSnapshot.readBytes
                ? snapshot.readBytes - m_lastFfmpegResourceSnapshot.readBytes : 0;
            const quint64 writeDelta = snapshot.writeBytes >= m_lastFfmpegResourceSnapshot.writeBytes
                ? snapshot.writeBytes - m_lastFfmpegResourceSnapshot.writeBytes : 0;
            readMbPerSecond = QString::number(
                static_cast<double>(readDelta) * 1000.0 / static_cast<double>(wallMs) / (1024.0 * 1024.0), 'f', 1);
            writeMbPerSecond = QString::number(
                static_cast<double>(writeDelta) * 1000.0 / static_cast<double>(wallMs) / (1024.0 * 1024.0), 'f', 1);
        }
    }

    QString systemCpuPercent = QStringLiteral("unavailable");
    if (m_lastFfmpegResourceSnapshot.system.available && snapshot.system.available) {
        const quint64 totalDelta = snapshot.system.cpuTimeMs >= m_lastFfmpegResourceSnapshot.system.cpuTimeMs
            ? snapshot.system.cpuTimeMs - m_lastFfmpegResourceSnapshot.system.cpuTimeMs : 0;
        const quint64 idleDelta = snapshot.system.idleTimeMs >= m_lastFfmpegResourceSnapshot.system.idleTimeMs
            ? snapshot.system.idleTimeMs - m_lastFfmpegResourceSnapshot.system.idleTimeMs : 0;
        if (totalDelta > 0 && idleDelta <= totalDelta) {
            systemCpuPercent = QString::number(
                100.0 * static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta), 'f', 1);
        }
    }

    qInfo() << "[YtDlpWorker][ffmpeg telemetry]"
            << "id=" << m_id << "stage=" << m_ffmpegStage
            << "elapsed_ms=" << (m_ffmpegStageTimer.isValid() ? m_ffmpegStageTimer.elapsed() : -1)
            << "pid=" << m_process->processId()
            << "root_image=" << snapshot.rootImageName
            << "root_parent_pid=" << snapshot.rootParentPid
            << "root_creation=" << snapshot.rootCreationTime
            << "process_count=" << snapshot.processCount
            << "child_pids=" << snapshot.processIds
            << "process_tree=" << formatProcessTree(snapshot)
            << "rss_mb=" << (snapshot.available ? QString::number(static_cast<double>(snapshot.residentBytes) / (1024.0 * 1024.0), 'f', 1) : QStringLiteral("unavailable"))
            << "cpu_percent=" << cpuPercent
            << "read_mb_s=" << readMbPerSecond
            << "write_mb_s=" << writeMbPerSecond
            << "system_cpu_percent=" << systemCpuPercent
            << "system_memory_available_mb=" << (snapshot.system.available ? QString::number(static_cast<double>(snapshot.system.memoryAvailableBytes) / (1024.0 * 1024.0), 'f', 1) : QStringLiteral("unavailable"))
            << "system_memory_total_mb=" << (snapshot.system.available ? QString::number(static_cast<double>(snapshot.system.memoryTotalBytes) / (1024.0 * 1024.0), 'f', 1) : QStringLiteral("unavailable"))
            << "tree_changed=" << processTreeChanged
            << "identity_changed=" << processIdentityChanged
            << "priority=" << (snapshot.priorityClass.isEmpty() ? QStringLiteral("unavailable") : snapshot.priorityClass)
            << "platform=" << snapshot.platform
            << "frame=" << progressValue(m_ffmpegProgressFields, QStringLiteral("frame"))
            << "fps=" << progressValue(m_ffmpegProgressFields, QStringLiteral("fps"))
            << "out_time_ms=" << progressValue(m_ffmpegProgressFields, QStringLiteral("out_time_ms"))
            << "speed=" << progressValue(m_ffmpegProgressFields, QStringLiteral("speed"));

    m_lastFfmpegTelemetryMs = nowMs;
    m_lastFfmpegResourceSnapshot = snapshot;
}

void YtDlpWorker::observeFfmpegDiagnosticLine(const QString &line)
{
    const QString stage = stageNameFromLine(line);
    if (!stage.isEmpty()) {
        beginFfmpegStage(stage);
        return;
    }

    if (m_ffmpegStage.isEmpty()) {
        return;
    }

    QString key;
    QString value;
    if (!parseFfmpegProgressLine(line, &key, &value)) {
        return;
    }

    static const QSet<QString> progressKeys = {
        QStringLiteral("frame"), QStringLiteral("fps"), QStringLiteral("bitrate"),
        QStringLiteral("total_size"), QStringLiteral("out_time_us"),
        QStringLiteral("out_time_ms"), QStringLiteral("speed"),
        QStringLiteral("progress")
    };
    if (!progressKeys.contains(key)) {
        return;
    }
    m_ffmpegProgressFields.insert(key, value);

    if (key == QLatin1String("progress") && value == QLatin1String("end")) {
        finishFfmpegStage(QStringLiteral("progress_end"));
        return;
    }

    if (m_ffmpegStageTimer.isValid() && m_ffmpegStageTimer.elapsed() >= FFMPEG_PROGRESS_LOG_INTERVAL_MS) {
        const qint64 lastLog = m_ffmpegProgressFields.value(QStringLiteral("_last_log_ms"), -1).toLongLong();
        const qint64 elapsed = m_ffmpegStageTimer.elapsed();
        if (lastLog < 0 || elapsed - lastLog >= FFMPEG_PROGRESS_LOG_INTERVAL_MS) {
            qDebug() << "[YtDlpWorker][ffmpeg progress]"
                     << "id=" << m_id << "stage=" << m_ffmpegStage
                     << "elapsed_ms=" << elapsed
                     << "frame=" << progressValue(m_ffmpegProgressFields, QStringLiteral("frame"))
                     << "fps=" << progressValue(m_ffmpegProgressFields, QStringLiteral("fps"))
                     << "out_time_ms=" << progressValue(m_ffmpegProgressFields, QStringLiteral("out_time_ms"))
                     << "speed=" << progressValue(m_ffmpegProgressFields, QStringLiteral("speed"));
            m_ffmpegProgressFields.insert(QStringLiteral("_last_log_ms"), elapsed);
        }
    }
}
