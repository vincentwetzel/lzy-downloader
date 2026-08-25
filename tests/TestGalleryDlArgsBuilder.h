#ifndef TESTGALLERYDLARGSBUILDER_H
#define TESTGALLERYDLARGSBUILDER_H

#include <QtTest/QtTest>
#include "BaseTest.h"

class TestGalleryDlArgsBuilder : public BaseTest {
    Q_OBJECT

private slots:
    void testBasicBuild();
    void testRateLimit();
    void testCookiesFromBrowser();
    void testOverrideArchive();
    void testRestrictFilenames();
};

#endif // TESTGALLERYDLARGSBUILDER_H
