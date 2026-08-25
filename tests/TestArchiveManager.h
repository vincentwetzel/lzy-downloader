#pragma once

#include <QtTest/QtTest>
#include "core/ArchiveManager.h"
#include "BaseTest.h"

class TestArchiveManager : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testUrlNormalization();
    void testConcurrentAccess();

private:
    ArchiveManager *m_archiveManager = nullptr;
    QString m_dbPath;
};