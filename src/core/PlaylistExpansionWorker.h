#pragma once

#include "ConfigManager.h"

#include <QObject>
#include <QList>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>

class PlaylistExpansionWorker : public QObject {
    Q_OBJECT

public:
    explicit PlaylistExpansionWorker(const QString &url, ConfigManager *configManager, QObject *parent = nullptr);
    void startExpansion(const QString &playlistLogic);

signals:
    void expansionFinished(const QString &originalUrl, const QList<QVariantMap> &expandedItems, const QString &error);
    void playlistDetected(const QString &url, int itemCount, const QVariantMap &options, const QList<QVariantMap> &expandedItems);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QStringList buildProbeArguments(const QString &playlistLogic);

    QString m_url;
    ConfigManager *m_configManager = nullptr;
    QProcess *m_process = nullptr;
    QString m_currentPlaylistLogic;
    QVariantMap m_options;
};
