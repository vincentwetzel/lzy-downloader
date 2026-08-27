#include "TestUIWidgets.h"
#include "core/ConfigManager.h"
#include "ui/advanced_settings/BinariesPage.h"
#include <QSignalSpy>
#include <QVariantMap>
#include <QPushButton>
#include <QGroupBox>
#include <QScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QTimer>

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

QTEST_MAIN(TestUIWidgets)
