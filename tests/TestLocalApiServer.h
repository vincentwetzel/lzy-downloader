#pragma once

#include <QtTest/QtTest>
#include "core/LocalApiServer.h"
#include "BaseTest.h"
#include <QUrl>

class TestLocalApiServer : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testStartupAndShutdown();
    void testApiTokenGeneration();
    void testConfiguredPort();
    void testUnauthorizedAccess();
    void testUrlUtilsExtractsSharedText();
    void testValidEnqueueRequest();
    void testValidCancelRequest();
    void testClientScopedStatusAndCancellation();

private:
    QUrl apiUrl(const QString &path) const;
    LocalApiServer *m_apiServer = nullptr;
};
