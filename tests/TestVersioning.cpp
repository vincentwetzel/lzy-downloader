#include "core/VersionParser.h"
#include "core/version.h"

#include <QtTest/QtTest>

class TestVersioning : public QObject {
    Q_OBJECT

private slots:
    void generatedVersionIncludesBuildNumber()
    {
        QString expected = QStringLiteral("%1.%2.%3")
                               .arg(APP_VERSION_MAJOR)
                               .arg(APP_VERSION_MINOR)
                               .arg(APP_VERSION_PATCH);
        if (LZY_EXPECTED_BUILD_NUMBER != 0) {
            expected += QStringLiteral(".%1").arg(LZY_EXPECTED_BUILD_NUMBER);
        }
        QCOMPARE(QString::fromLatin1(APP_VERSION_STRING), expected);
    }

    void releaseTagsCompareByBuildNumber()
    {
        const Version base = Version::parse(QStringLiteral("v1.2.56"));
        const Version firstBuild = Version::parse(QStringLiteral("v1.2.56.123"));
        const Version secondBuild = Version::parse(QStringLiteral("v1.2.56.124"));

        QVERIFY(firstBuild > base);
        QVERIFY(secondBuild > firstBuild);
    }
};

QTEST_GUILESS_MAIN(TestVersioning)

#include "TestVersioning.moc"
