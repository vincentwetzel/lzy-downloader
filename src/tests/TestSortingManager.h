#ifndef TESTSORTINGMANAGER_H
#define TESTSORTINGMANAGER_H

#include <QtTest/QtTest>
#include "core/SortingManager.h"
#include "core/ConfigManager.h"
#include "BaseTest.h"

class TestSortingManager : public BaseTest {
    Q_OBJECT

private slots:
    void testCustomTokenEvaluation_data();
    void testCustomTokenEvaluation();
    void testIllegalCharacterSanitization_data();
    void testIllegalCharacterSanitization();
    void testSanitizeSpecificChars();

private:
    SortingManager *m_sortingManager;

protected:
    void init() override {
        BaseTest::init();
        m_sortingManager = new SortingManager(getConfigManager(), this);
    }

    void cleanup() override {
        delete m_sortingManager;
        m_sortingManager = nullptr;
        BaseTest::cleanup();
    }
};

#endif // TESTSORTINGMANAGER_H
