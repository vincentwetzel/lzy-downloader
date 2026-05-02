#ifndef TESTYTDLPARGSBUILDER_H
#define TESTYTDLPARGSBUILDER_H

#include <QtTest/QtTest>
#include <QUrl> // Add QUrl include
#include "core/YtDlpArgsBuilder.h"
#include "core/ConfigManager.h"
#include "BaseTest.h" // Include BaseTest

class TestYtDlpArgsBuilder : public BaseTest { // Inherit from BaseTest
    Q_OBJECT

private slots:
    void testBasicVideoArguments();
    void testSponsorBlockArguments();
};

#endif // TESTYTDLPARGSBUILDER_H
