#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>

#include "core/DiagnosticTail.h"

class ConfigManager;

class GalleryDlWorker : public QObject
{
    Q_OBJECT
public:
    explicit GalleryDlWorker(const QString &id, const QStringList &args, ConfigManager *configManager, QObject *parent = nullptr);
    void start();
    void killProcess();

signals:
    void progressUpdated(const QString &id, const QVariantMap &progressData);
    void finished(const QString &id, bool success, const QString &message, const QString &finalFilename, const QVariantMap &metadata);
    void outputReceived(const QString &id, const QString &output);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError processError);

private:
    QString resolveExecutablePath(const QString &name) const;

    ConfigManager *m_configManager;
    QString m_id;
    QStringList m_args;
    QProcess *m_process;
    QString m_lastFile;
    QByteArray m_outputBuffer;
    QByteArray m_errorBuffer;
    DiagnosticTail m_diagnosticTail{400, 64 * 1024};
};
