#include "TestUIWidgets.h"
#include <QSignalSpy>
#include <QVariantMap>

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

QTEST_MAIN(TestUIWidgets)
