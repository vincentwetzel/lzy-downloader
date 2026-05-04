#ifndef TESTARCHIVEMANAGER_H
#define TESTARCHIVEMANAGER_H

#include <QtTest/QtTest>
#include "core/ArchiveManager.h"
#include "core/ConfigManager.h"
#include "BaseTest.h"

class TestArchiveManager : public BaseTest {
    Q_OBJECT

private slots:
    void testUrlNormalization_data();
    void testUrlNormalization();

private:
    ArchiveManager *m_archiveManager;

protected:
    void init() override {
        BaseTest::init();
        // Use the constructor for testing that injects a temporary database path
        m_archiveManager = new ArchiveManager(getConfigManager(), getTempDir() + "/test_archive.db", true, this);
    }

    void cleanup() override {
        delete m_archiveManager;
        m_archiveManager = nullptr;
        BaseTest::cleanup();
    }
};

#endif // TESTARCHIVEMANAGER_H
