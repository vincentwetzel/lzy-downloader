#pragma once

#include <QtTest/QtTest>
#include "core/ProcessUtils.h"
#include "BaseTest.h"

class TestProcessUtils : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testCacheHit();
    void testCacheInvalidation();
};