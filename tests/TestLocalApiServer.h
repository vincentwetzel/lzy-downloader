#pragma once

#include <QtTest/QtTest>
#include "core/LocalApiServer.h"
#include "BaseTest.h"

class TestLocalApiServer : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testStartupAndShutdown();
    void testApiTokenGeneration();
    void testUnauthorizedAccess();
    void testValidEnqueueRequest();

private:
    LocalApiServer *m_apiServer = nullptr;
};