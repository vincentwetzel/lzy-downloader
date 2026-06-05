#pragma once

#include <QObject>
#include <QTextEdit>
#include <QComboBox>
#include <QDir>

#include "core/ConfigManager.h"
#include "ui/StartTabUiBuilder.h"
#include "core/YtDlpArgsBuilder.h"
#include "core/GalleryDlArgsBuilder.h"
#include "core/ProcessUtils.h" // For resolveExecutablePath
#include "ui/ToggleSwitch.h" // For ToggleSwitch

class StartTabCommandPreviewUpdater : public QObject
{
    Q_OBJECT

public:
    explicit StartTabCommandPreviewUpdater(ConfigManager *configManager, StartTabUiBuilder *uiBuilder,
                                           YtDlpArgsBuilder *ytDlpArgsBuilder, GalleryDlArgsBuilder *galleryDlArgsBuilder,
                                           QObject *parent = nullptr);
    ~StartTabCommandPreviewUpdater();

public slots:
    void updateCommandPreview();

private:
    ConfigManager *m_configManager;
    StartTabUiBuilder *m_uiBuilder;
    YtDlpArgsBuilder *m_ytDlpArgsBuilder;
    GalleryDlArgsBuilder *m_galleryDlArgsBuilder;

    QString resolveExecutablePath(const QString &name) const;
};

