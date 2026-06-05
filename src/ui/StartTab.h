#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QVariantMap>
#include <QProcess>
#include <QSpinBox>
#include <QGroupBox>
#include <QMessageBox>
#include "core/ConfigManager.h"
#include "utils/ExtractorJsonParser.h"
#include "core/YtDlpArgsBuilder.h"
#include "core/GalleryDlArgsBuilder.h"

class QEvent;
class QFocusEvent;
class ToggleSwitch;
class StartTabUiBuilder;
class StartTabUrlHandler;
class StartTabDownloadActions;
class StartTabCommandPreviewUpdater;

class StartTab : public QWidget {
    Q_OBJECT

public:
    explicit StartTab(ConfigManager *configManager, ExtractorJsonParser *extractorJsonParser, QWidget *parent = nullptr);
    ~StartTab() override;
    bool tryAutoPasteFromClipboard();
    void focusUrlInput();

protected:
    void focusInEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void changeEvent(QEvent *event) override;

signals:
    void downloadRequested(const QString &url, const QVariantMap &options);
    void navigateToExternalBinaries();
    void missingBinariesDetected(const QStringList &missingBinaries);
    void urlInputTextChanged(const QString &text);

public slots: // Changed from private slots:
    void onDownloadButtonClicked();
    void onExtractorsReady();
    void updateCommandPreview();
    void onDuplicateDownloadDetected(const QString &url, const QString &reason);
    void updateDynamicUI();

private:
    void setupUI();
    void loadSettings();
    void applyUrlInputStyleSheet();
    void applyCommandPreviewStyleSheet(); // Added this line

    enum class ExtractorSupport {
        None,
        YtDlpOnly,
        GalleryDlOnly,
        Both
    };

    ConfigManager *m_configManager;
    ExtractorJsonParser *m_extractorJsonParser;
    YtDlpArgsBuilder *m_ytDlpArgsBuilder;
    GalleryDlArgsBuilder *m_galleryDlArgsBuilder;

    StartTabUiBuilder *m_uiBuilder;
    StartTabUrlHandler *m_urlHandler;
    StartTabDownloadActions *m_downloadActions;
    StartTabCommandPreviewUpdater *m_commandPreviewUpdater;

    QMessageBox *m_typeSelectionDialog = nullptr;
    QString m_lastAutoSwitchedUrl;
};

