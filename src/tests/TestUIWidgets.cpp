#include "TestUIWidgets.h"
#include <QSignalSpy>
#include <QVariantMap>
#include <QLabel>
#include <QPushButton>

void TestUIWidgets::testProgressLabelBarFilling() {
    ProgressLabelBar progressBar;
    progressBar.setRange(0, 100);

    progressBar.setValue(0);
    progressBar.setProgressText("0%");
    QCOMPARE(progressBar.value(), 0);
    QCOMPARE(progressBar.progressText(), "0%");

    progressBar.setValue(50);
    progressBar.setProgressText("50% - 10MB/s - 00:00:10");
    QCOMPARE(progressBar.value(), 50);
    QCOMPARE(progressBar.progressText(), "50% - 10MB/s - 00:00:10");

    progressBar.setValue(100);
    progressBar.setProgressText("Completed");
    QCOMPARE(progressBar.value(), 100);
    QCOMPARE(progressBar.progressText(), "Completed");
}

void TestUIWidgets::testDownloadItemWidgetFinishedState() {
    QVariantMap itemData;
    itemData["id"] = "test-id";
    itemData["title"] = "Test Video";
    itemData["url"] = "https://example.com/test";

    DownloadItemWidget widget(itemData);

    // Initial state
    QCOMPARE(widget.isFinished(), false);
    QCOMPARE(widget.isSuccessful(), false);

    // Test successful completion
    widget.setFinished(true, "Download Complete");
    QCOMPARE(widget.isFinished(), true);
    QCOMPARE(widget.isSuccessful(), true);
    QCOMPARE(widget.findChild<ProgressLabelBar*>()->styleSheet(), "QProgressBar::chunk { background-color: #22c55e; }");

    // Test cancelled state
    widget.setCancelled();
    QCOMPARE(widget.isFinished(), true);
    QCOMPARE(widget.isSuccessful(), false);
    QCOMPARE(widget.findChild<ProgressLabelBar*>()->styleSheet(), "QProgressBar { color: #dc2626; }");

    // Test failed state
    widget.setFinished(false, "Download Failed");
    QCOMPARE(widget.isFinished(), true);
    QCOMPARE(widget.isSuccessful(), false);
    QCOMPARE(widget.findChild<ProgressLabelBar*>()->styleSheet(), "QProgressBar { color: #dc2626; }");

    // Assertion for color change (tricky in headless)
    // The TODO.md states: "assert that the progress bar fills up and changes its stylesheet color to green (#22c55e)."
    // Inspecting stylesheet properties directly in QTest for a custom painted widget is non-trivial.
    // One approach could be to make the color a settable property of ProgressLabelBar and check that.
    // Or, if the color change logic is handled by changing a CSS class, we could check the class name.
    // For now, I'll leave a comment, as direct visual inspection is not possible.
    // This might require a deeper dive into Qt's styling system or modifying the widget to expose its color state.
}

QTEST_MAIN(TestUIWidgets)

#include "TestUIWidgets.moc"
