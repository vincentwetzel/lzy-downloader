#pragma once

#include <QtTest/QtTest>
#include "core/UrlValidator.h"
#include "BaseTest.h"

class TestUrlValidator : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testTier1FastTrackYoutube();
    void testInvalidUrlRejection();
    void testTier2Simulation();

private:
    UrlValidator *m_validator = nullptr;
};