#ifndef TESTUIWIDGETS_H
#define TESTUIWIDGETS_H

#include <QtTest/QtTest>
#include "BaseTest.h"
#include "ui/DownloadItemWidget.h" // Includes ProgressLabelBar

class TestUIWidgets : public BaseTest {
    Q_OBJECT

private slots:
    void testProgressLabelBarFilling();
    void testDownloadItemWidgetFinishedState();

private slots:
    void init() {
        BaseTest::init();
    }

    void cleanup() {
        BaseTest::cleanup();
    }
};

#endif // TESTUIWIDGETS_H
