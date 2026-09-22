#pragma once

#include <QObject>
#include <QByteArray>
#include <QProcess>
#include <QElapsedTimer>
#include <QString>
#include <QVariantMap>

class ConfigManager;

class MetadataEmbedder : public QObject {
    Q_OBJECT

public:
    explicit MetadataEmbedder(ConfigManager *configManager, QObject *parent = nullptr);
    static bool supportsAttachedPicture(const QString &filePath);
    /**
     * @brief Sets the local artwork file for the metadata remux path.
     *
     * @param thumbnailPath Absolute path to the downloaded thumbnail image.
     */
    void setThumbnailPath(const QString &thumbnailPath);
    void setExtraMetadata(const QVariantMap &metadata);
    void processFile(const QString &filePath, int trackNumber, bool normalizeContainerTimestamps);
    void cancel();

signals:
    void finished(bool success, const QString &error);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    enum class Stage {
        Idle,
        ProbingDuration,
        RewritingFile
    };

    void startDurationProbe();
    void startRewrite();
    void appendProcessOutput(const QByteArray &data);

    QProcess *m_process;
    ConfigManager *m_configManager;
    QString m_tempFilePath;
    QString m_originalFilePath;
    int m_pendingTrackNumber;
    bool m_normalizeContainerTimestamps;
    double m_targetDurationSeconds;
    Stage m_stage;
    QString m_processOutputTail;
    QString m_thumbnailPath;
    QVariantMap m_extraMetadata;
    QElapsedTimer m_processTimer;
    qint64 m_lastProgressLogMs = -1;
    QString m_lastProgressFrame;
    QString m_lastProgressTime;
    QString m_lastProgressSpeed;
};

