#include "BaseTest.h"
#include "core/FileReplacement.h"

#include <QFile>
#include <QDir>
#include <QtTest/QtTest>

class TestFileReplacement : public BaseTest {
    Q_OBJECT

private slots:
    void testReplacementPreservesDestinationUntilMoveSucceeds();
    void testMissingSourceLeavesDestinationUntouched();
};

void TestFileReplacement::testReplacementPreservesDestinationUntilMoveSucceeds()
{
    const QDir directory(getTempDir());
    const QString source = directory.filePath(QStringLiteral("new.part"));
    const QString destination = directory.filePath(QStringLiteral("final.mp4"));

    QFile oldFile(destination);
    QVERIFY(oldFile.open(QIODevice::WriteOnly));
    oldFile.write("old");
    oldFile.close();

    QFile newFile(source);
    QVERIFY(newFile.open(QIODevice::WriteOnly));
    newFile.write("new");
    newFile.close();

    QVERIFY(FileReplacement::moveReplacing(source, destination));
    QVERIFY(!QFile::exists(source));
    QFile result(destination);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray("new"));
}

void TestFileReplacement::testMissingSourceLeavesDestinationUntouched()
{
    const QDir directory(getTempDir());
    const QString source = directory.filePath(QStringLiteral("missing.part"));
    const QString destination = directory.filePath(QStringLiteral("existing.mp4"));

    QFile oldFile(destination);
    QVERIFY(oldFile.open(QIODevice::WriteOnly));
    oldFile.write("old");
    oldFile.close();

    QVERIFY(!FileReplacement::moveReplacing(source, destination));
    QFile result(destination);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray("old"));
}

QTEST_GUILESS_MAIN(TestFileReplacement)
#include "TestFileReplacement.moc"
