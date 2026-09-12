#include "TestUIWidgets.h"
#include "core/ConfigManager.h"
#include "core/ProcessUtils.h"
#include "ui/MainWindowHelpers.h"
#include "ui/StartTab.h"
#include "ui/MissingBinariesDialog.h"
#include "ui/advanced_settings/BinariesPage.h"
#include "ui/AdvancedSettingsTab.h"
#include "ui/DownloadHistoryTab.h"
#include <QSignalSpy>
#include <QVariantMap>
#include <QPushButton>
#include <QGroupBox>
#include <QScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QLabel>
#include <QPair>

void TestUIWidgets::testAdvancedSettingsSectionsUseNaturalSpacing() {
    AdvancedSettingsTab tab(getConfigManager());
    tab.resize(900, 700);
    tab.show();
    QCoreApplication::processEvents();

    const QList<QGroupBox *> groups = tab.findChildren<QGroupBox *>();
    QGroupBox *configuration = nullptr;
    QGroupBox *authentication = nullptr;
    for (QGroupBox *group : groups) {
        if (group->title() == QStringLiteral("Configuration")) {
            configuration = group;
        } else if (group->title() == QStringLiteral("Authentication Access")) {
            authentication = group;
        }
    }

    QVERIFY(configuration != nullptr);
    QVERIFY(authentication != nullptr);
    const int configurationBottom = configuration->mapTo(&tab, QPoint(0, configuration->height())).y();
    const int authenticationTop = authentication->mapTo(&tab, QPoint(0, 0)).y();
    QVERIFY(authenticationTop - configurationBottom < 40);
}

void TestUIWidgets::testStartTabCarriesPlaylistLogicIntoRequest() {
    ConfigManager *config = getConfigManager();
    StartTab startTab(config, nullptr);

    QComboBox *playlistLogicCombo = nullptr;
    for (QComboBox *combo : startTab.findChildren<QComboBox *>()) {
        if (combo->findData(QStringLiteral("Download All (no prompt)")) >= 0) {
            playlistLogicCombo = combo;
            break;
        }
    }
    QVERIFY(playlistLogicCombo != nullptr);
    const int allIndex = playlistLogicCombo->findData(QStringLiteral("Download All (no prompt)"));
    QVERIFY(allIndex >= 0);
    playlistLogicCombo->setCurrentIndex(allIndex);

    QTextEdit *urlInput = startTab.findChild<QTextEdit *>();
    QVERIFY(urlInput != nullptr);
    urlInput->setPlainText(QStringLiteral("https://media.example.test/playlist?id=regression"));

    QSignalSpy requestSpy(&startTab, &StartTab::downloadRequested);
    startTab.onDownloadButtonClicked();

    QCOMPARE(requestSpy.count(), 1);
    const QVariantMap options = requestSpy.at(0).at(1).toMap();
    QCOMPARE(options.value(QStringLiteral("playlist_logic")).toString(),
             QStringLiteral("Download All (no prompt)"));
    QCOMPARE(config->get(QStringLiteral("General"), QStringLiteral("playlist_logic")).toString(),
             QStringLiteral("Download All (no prompt)"));
}

void TestUIWidgets::testProgressLabelBarFilling() {
    ProgressLabelBar progressBar;
    progressBar.setRange(0, 100);

    progressBar.setValue(0);
    progressBar.setProgressText(QStringLiteral("0%"));
    QCOMPARE(progressBar.value(), 0);
    QCOMPARE(progressBar.progressText(), QStringLiteral("0%"));

    progressBar.setValue(50);
    progressBar.setProgressText(QStringLiteral("50% - 10MB/s - 00:00:10"));
    QCOMPARE(progressBar.value(), 50);
    QCOMPARE(progressBar.progressText(), QStringLiteral("50% - 10MB/s - 00:00:10"));

    progressBar.setValue(100);
    progressBar.setProgressText(QStringLiteral("Completed"));
    QCOMPARE(progressBar.value(), 100);
    QCOMPARE(progressBar.progressText(), QStringLiteral("Completed"));
}

void TestUIWidgets::testDownloadItemWidgetFinishedState() {
    QVariantMap itemData;
    itemData[QStringLiteral("id")] = QStringLiteral("test-id");
    itemData[QStringLiteral("title")] = QStringLiteral("Test Video");
    itemData[QStringLiteral("url")] = QStringLiteral("https://example.com/test");

    DownloadItemWidget widget(itemData);

    // Initial state
    QCOMPARE(widget.isFinished(), false);
    QCOMPARE(widget.isSuccessful(), false);

    // Test successful completion
    widget.setFinished(true, QStringLiteral("Download Complete"));
    QCOMPARE(widget.isFinished(), true);
    QCOMPARE(widget.isSuccessful(), true);
    ProgressLabelBar *progressBar = widget.findChild<ProgressLabelBar*>();
    QVERIFY(progressBar != nullptr);
    QCOMPARE(widget.findChildren<QProgressBar*>().size(), 1);

    // Test cancelled state
    widget.setCancelled();
    QCOMPARE(widget.isFinished(), true);
    QCOMPARE(widget.isSuccessful(), false);
    progressBar = widget.findChild<ProgressLabelBar*>();
    QVERIFY(progressBar != nullptr);

    // Test failed state
    widget.setFinished(false, QStringLiteral("Download Failed"));
    QCOMPARE(widget.isFinished(), true);
    QCOMPARE(widget.isSuccessful(), false);
    progressBar = widget.findChild<ProgressLabelBar*>();
    QVERIFY(progressBar != nullptr);
}

void TestUIWidgets::testDownloadItemWidgetUsesAggregateProgressAcrossStreams() {
    QVariantMap itemData;
    itemData[QStringLiteral("id")] = QStringLiteral("aggregate-progress");
    DownloadItemWidget widget(itemData);
    ProgressLabelBar *progressBar = widget.findChild<ProgressLabelBar *>();
    QVERIFY(progressBar != nullptr);

    widget.updateProgress({
        {QStringLiteral("progress"), 100.0},
        {QStringLiteral("overall_progress"), 66.0},
        {QStringLiteral("status"), QStringLiteral("Downloading video stream...")}
    });
    QTRY_COMPARE(progressBar->value(), 66);

    // The active audio stream starts at 87%, but the aggregate download has
    // advanced to 91%; the visible bar must not jump back to 87%.
    widget.updateProgress({
        {QStringLiteral("progress"), 87.0},
        {QStringLiteral("overall_progress"), 91.0},
        {QStringLiteral("status"), QStringLiteral("Downloading audio stream...")}
    });
    QTRY_COMPARE(progressBar->value(), 91);

    // A delayed native line must not undo already visible aggregate progress.
    widget.updateProgress({
        {QStringLiteral("progress"), 93.0},
        {QStringLiteral("overall_progress"), 62.0},
        {QStringLiteral("status"), QStringLiteral("Downloading video stream...")}
    });
    QTRY_COMPARE(progressBar->value(), 91);
}

void TestUIWidgets::testDownloadItemWidgetCoalescesHighFrequencyProgress()
{
    QVariantMap itemData;
    itemData[QStringLiteral("id")] = QStringLiteral("progress-coalescing");
    DownloadItemWidget widget(itemData);
    ProgressLabelBar *progressBar = widget.findChild<ProgressLabelBar *>();
    QVERIFY(progressBar != nullptr);

    for (int progress = 1; progress <= 500; ++progress) {
        widget.updateProgress({
            {QStringLiteral("progress"), qMin(progress, 100)},
            {QStringLiteral("status"), QStringLiteral("Downloading...")}
        });
    }

    // Rendering is timer-coalesced; the synchronous signal burst must not
    // repaint the QWidget 500 times or apply stale intermediate values.
    QCOMPARE(progressBar->value(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(progressBar->progressText().startsWith(QStringLiteral("100%")), 2000);
}

void TestUIWidgets::testDownloadItemWidgetKeepsActionsVisibleWhenNarrow() {
    QVariantMap itemData;
    itemData[QStringLiteral("id")] = QStringLiteral("narrow-row");
    itemData[QStringLiteral("title")] = QStringLiteral("A very long title that must wrap instead of pushing the row actions outside the viewport");
    itemData[QStringLiteral("url")] = QStringLiteral("https://example.com/a-long-media-url");

    DownloadItemWidget widget(itemData);
    widget.resize(620, 120);
    widget.show();
    QCoreApplication::processEvents();

    QPushButton *cancelButton = nullptr;
    for (QPushButton *button : widget.findChildren<QPushButton*>()) {
        if (button->text() == QObject::tr("Cancel")) {
            cancelButton = button;
            break;
        }
    }

    QVERIFY(cancelButton != nullptr);
    QVERIFY(cancelButton->isVisible());
    QVERIFY(cancelButton->geometry().right() <= widget.rect().right());
}

void TestUIWidgets::testDownloadItemWidgetShowsMediaTypeIcon()
{
    const QList<QPair<QString, QString>> mediaTypes = {
        {QStringLiteral("video"), QStringLiteral("Video")},
        {QStringLiteral("audio"), QStringLiteral("Audio")},
        {QStringLiteral("gallery"), QStringLiteral("Gallery")}
    };

    for (const auto &[type, label] : mediaTypes) {
        QVariantMap itemData;
        itemData[QStringLiteral("id")] = type;
        itemData[QStringLiteral("options")] = QVariantMap{{QStringLiteral("type"), type}};

        DownloadItemWidget widget(itemData);
        QLabel *typeIcon = widget.findChild<QLabel *>(QStringLiteral("downloadTypeIcon"));

        QVERIFY(typeIcon != nullptr);
        QVERIFY(!typeIcon->pixmap(Qt::ReturnByValue).isNull());
        QCOMPARE(typeIcon->toolTip(), QObject::tr("Download type: %1").arg(label));
        QVERIFY(typeIcon->accessibleName().contains(label));
    }
}

void TestUIWidgets::testDownloadHistoryUpdatesThumbnailAfterAsyncCacheCopy()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString historyPath = QDir(tempDir.path()).filePath(QStringLiteral("history.json"));
    const QString thumbnailPath = QDir(tempDir.path()).filePath(QStringLiteral("cached.jpg"));
    QImage image(24, 16, QImage::Format_RGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(thumbnailPath, "JPG"));

    QJsonObject history;
    history[QStringLiteral("id")] = QStringLiteral("completed-id");
    history[QStringLiteral("title")] = QStringLiteral("Completed item");
    history[QStringLiteral("thumbnailPath")] = QStringLiteral("missing-thumbnail.jpg");
    QFile historyFile(historyPath);
    QVERIFY(historyFile.open(QIODevice::WriteOnly));
    QVERIFY(historyFile.write(QJsonDocument(QJsonArray{history}).toJson()) > 0);
    historyFile.close();

    DownloadHistoryTab tab;
    tab.setObjectName(QStringLiteral("downloadHistoryTab"));
    tab.loadHistory(historyPath);
    tab.updateThumbnail(QStringLiteral("completed-id"), thumbnailPath);

    QLabel *thumbnailLabel = nullptr;
    for (QLabel *label : tab.findChildren<QLabel *>()) {
        if (label->size() == QSize(120, 68)) {
            thumbnailLabel = label;
            break;
        }
    }
    QVERIFY(thumbnailLabel != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(!thumbnailLabel->pixmap(Qt::ReturnByValue).isNull(), 2000);

    bool savedThumbnail = false;
    QTRY_VERIFY_WITH_TIMEOUT(([&savedThumbnail, &historyPath, &thumbnailPath]() {
        QFile savedHistory(historyPath);
        if (!savedHistory.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QJsonArray saved = QJsonDocument::fromJson(savedHistory.readAll()).array();
        savedThumbnail = saved.size() == 1
            && saved.first().toObject().value(QStringLiteral("thumbnailPath")).toString() == thumbnailPath;
        return savedThumbnail;
    })(), 2000);
    QVERIFY(savedThumbnail);
}

void TestUIWidgets::testBinariesPageUsesNaturalScrollDocument() {
    BinariesPage page(getConfigManager());
    page.resize(520, 320);
    page.show();
    QCoreApplication::processEvents();

    QScrollArea *scrollArea = page.findChild<QScrollArea *>();
    QVERIFY(scrollArea != nullptr);
    QCOMPARE(scrollArea->widgetResizable(), false);
    QCOMPARE(scrollArea->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

    QGroupBox *document = qobject_cast<QGroupBox *>(scrollArea->widget());
    QVERIFY(document != nullptr);
    QVERIFY(document->layout() != nullptr);
    QTRY_COMPARE(document->width(), scrollArea->viewport()->width());
}

void TestUIWidgets::testDenoAppManagedInstallIsRecommended() {
    BinariesPage page(getConfigManager());
    bool dialogWasShown = false;
    bool firstOptionIsRecommended = false;

    QTimer::singleShot(0, &page, [&]() {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        dialogWasShown = true;
        auto *options = dialog->findChild<QComboBox *>(QStringLiteral("binaryInstallOptionsCombo"));
        if (options) {
            firstOptionIsRecommended = options->count() > 0 &&
                options->itemText(0).contains(QStringLiteral("(Recommended)"));
        }
        dialog->reject();
    });

    page.installBinaryFor(QStringLiteral("deno"));

    QVERIFY(dialogWasShown);
    QVERIFY(firstOptionIsRecommended);
}

void TestUIWidgets::testRequiredToolsDialogDistinguishesExistingUpdates() {
    ConfigManager *config = getConfigManager();
    config->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"), QCoreApplication::applicationFilePath());
    config->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_auto_detected"), false);
    config->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_update_available"), true);
    config->set(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_latest_version"), QStringLiteral("999.0"));
    config->save();
    ProcessUtils::clearCache();

    BinariesPage page(config);
    MissingBinariesDialog dialog({QStringLiteral("yt-dlp")}, config, &page,
                                 {{QStringLiteral("yt-dlp"), QStringLiteral("Test update")}});

    bool foundExistingUpdateAction = false;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Update existing")) {
            foundExistingUpdateAction = true;
            break;
        }
    }
    QVERIFY(foundExistingUpdateAction);

    bool foundUpdateAll = false;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Update All")) {
            foundUpdateAll = true;
            break;
        }
    }
    QVERIFY(foundUpdateAll);

    config->remove(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_path"));
    config->remove(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_auto_detected"));
    config->remove(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_update_available"));
    config->remove(QStringLiteral("Binaries"), QStringLiteral("yt-dlp_latest_version"));
    config->save();
    ProcessUtils::clearCache();

#ifdef Q_OS_WIN
    QCOMPARE(page.recommendedInstallLabel(QStringLiteral("deno")),
             QStringLiteral("PowerShell (Deno stable) (Recommended)"));
#endif
}

void TestUIWidgets::testApplicationUpdateBlocksConflictingClipboardAndDownloads()
{
    QVERIFY(MainWindowHelpers::blocksClipboardAutoPasteForApplicationUpdate(true, false, false));
    QVERIFY(MainWindowHelpers::blocksClipboardAutoPasteForApplicationUpdate(false, true, false));
    QVERIFY(MainWindowHelpers::blocksClipboardAutoPasteForApplicationUpdate(false, false, true));
    QVERIFY(!MainWindowHelpers::blocksClipboardAutoPasteForApplicationUpdate(false, false, false));

    QVERIFY(MainWindowHelpers::blocksDownloadAdmissionForApplicationUpdate(true, false));
    QVERIFY(MainWindowHelpers::blocksDownloadAdmissionForApplicationUpdate(false, true));
    QVERIFY(!MainWindowHelpers::blocksDownloadAdmissionForApplicationUpdate(false, false));
}

QTEST_MAIN(TestUIWidgets)
