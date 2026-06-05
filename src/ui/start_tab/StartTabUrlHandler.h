#pragma once

#include <QObject>
#include <QClipboard>
#include <QMessageBox>
#include <QTextEdit>
#include <QComboBox>
#include <QFocusEvent>
#include <QEvent>
#include "core/ConfigManager.h"
#include "utils/ExtractorJsonParser.h"
#include "ui/StartTabUiBuilder.h" // To access UI elements

class StartTabUrlHandler : public QObject
{
    Q_OBJECT

public:
    explicit StartTabUrlHandler(ConfigManager *configManager, ExtractorJsonParser *extractorJsonParser, StartTabUiBuilder *uiBuilder, QObject *parent = nullptr);
    ~StartTabUrlHandler();

    bool tryAutoPasteFromClipboard();
    void focusUrlInput();

signals:
    void urlInputTextChanged(const QString &text); // Re-emit for StartTab to connect to other parts

public slots:
    void onExtractorsReady();
    void onClipboardChangedWhileDialogIsOpen();
    void onTypeSelectionDialogFinished(int result);
    void onUrlInputTextChanged(); // Slot for actual text change
    void handleFocusInEvent(QFocusEvent *event); // To be called from StartTab's focusInEvent
    bool handleEventFilter(QObject *obj, QEvent *event); // To be called from StartTab's eventFilter

private:
    enum class ExtractorSupport {
        None,
        YtDlpOnly,
        GalleryDlOnly,
        Both
    };

    ConfigManager *m_configManager;
    ExtractorJsonParser *m_extractorJsonParser;
    StartTabUiBuilder *m_uiBuilder;

    QMessageBox *m_typeSelectionDialog;
    QString m_lastAutoSwitchedUrl;

    bool checkClipboardForUrl();
    ExtractorSupport checkUrlExtractorSupport(const QString &url) const;
    void autoSwitchDownloadType(const QString &url);
};