#include "core/ConfigManager.h"
#include "core/StartupWorker.h"
#include "utils/ExtractorJsonParser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class TestStartupWorker : public QObject {
    Q_OBJECT

private slots:
    void finishesWhenBinaryVersionProbesFail();
};

void TestStartupWorker::finishesWhenBinaryVersionProbesFail()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    ConfigManager config(QDir(temporaryDir.path()).filePath(QStringLiteral("settings.ini")), true);
    const QString probeTarget = QCoreApplication::applicationFilePath();
    QVERIFY(QFileInfo::exists(probeTarget));

    const QStringList binaryNames = {
        QStringLiteral("yt-dlp"), QStringLiteral("ffmpeg"), QStringLiteral("ffprobe"),
        QStringLiteral("gallery-dl"), QStringLiteral("aria2c"), QStringLiteral("deno")};
    for (const QString &binaryName : binaryNames) {
        config.set(QStringLiteral("Binaries"), binaryName + QStringLiteral("_path"), probeTarget);
        config.set(QStringLiteral("Binaries"), binaryName + QStringLiteral("_auto_detected"), false);
    }
    config.save();

    ExtractorJsonParser extractorParser;
    StartupWorker worker(&config, &extractorParser);
    QSignalSpy finishedSpy(&worker, &StartupWorker::finished);

    worker.start();

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
}

QTEST_GUILESS_MAIN(TestStartupWorker)

#include "TestStartupWorker.moc"
