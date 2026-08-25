#pragma once

#include <QtTest/QtTest>
#include "core/ConfigManager.h"
#include "BaseTest.h"

class TestConfigManager : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testDefaultValues();
    void testSetAndGet();
    void testLegacyCleanup();
    void testResetToDefaults();

private:
    ConfigManager *m_configManager = nullptr;
};