#pragma once

#include <QObject>
#include <QProcess>
#include "ConfigManager.h"

class UrlValidator : public QObject {
    Q_OBJECT

public:
    explicit UrlValidator(ConfigManager *configManager, QObject *parent = nullptr);
    void validate(const QString &url);

signals:
    void validationFinished(bool isValid, const QString &error);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    ConfigManager *m_configManager;
    QProcess *m_process;
};