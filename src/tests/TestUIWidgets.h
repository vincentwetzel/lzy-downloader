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

protected:
    void init() override {
        BaseTest::init();
    }

    void cleanup() override {
        BaseTest::cleanup();
    }
};

#endif // TESTUIWIDGETS_H
