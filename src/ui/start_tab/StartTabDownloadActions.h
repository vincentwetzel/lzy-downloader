#pragma once

#include <QObject>
#include <QMessageBox>
#include <QProcess>
#include <QDesktopServices>
#include <QDir>
#include <QStandardItemModel>
#include <QVBoxLayout> // For dialog layout
#include <QTextEdit> // For dialog text
#include <QPushButton> // For dialog button
#include <QStringList>

#include "core/ConfigManager.h"
#include "ui/StartTabUiBuilder.h"
#include "core/YtDlpArgsBuilder.h"
#include "core/GalleryDlArgsBuilder.h"
#include "core/ProcessUtils.h" // For resolveExecutablePath and findBinary

class StartTabDownloadActions : public QObject
{
    Q_OBJECT

public:
    explicit StartTabDownloadActions(ConfigManager *configManager, StartTabUiBuilder *uiBuilder,
                                     YtDlpArgsBuilder *ytDlpArgsBuilder, GalleryDlArgsBuilder *galleryDlArgsBuilder,
                                     QWidget *parent = nullptr);
    ~StartTabDownloadActions();

public slots:
    void onDownloadButtonClicked();
    void onDownloadTypeChanged(int index);
    void onViewFormatsFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void openDownloadsFolder();
    void updateDynamicUI(); // Moved here as it affects download button state

signals:
    void downloadRequested(const QString &url, const QVariantMap &options);
    void navigateToExternalBinaries(); // Signal to StartTab to re-emit to MainWindow
    void missingBinariesDetected(const QStringList &missingBinaries);
    void updateCommandPreview(); // Signal to StartTabCommandPreviewUpdater

private:
    ConfigManager *m_configManager;
    StartTabUiBuilder *m_uiBuilder;
    YtDlpArgsBuilder *m_ytDlpArgsBuilder; // Needed for checkFormats
    GalleryDlArgsBuilder *m_galleryDlArgsBuilder; // Not directly used here, but passed to constructor

    QWidget *m_parentWidget; // To use for QMessageBox parent

    void checkFormats(const QString &url);
    QString resolveExecutablePath(const QString &name) const;
};

