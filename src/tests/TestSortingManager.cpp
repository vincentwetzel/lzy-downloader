#include "TestSortingManager.h"
#include <QVariantMap>

void TestSortingManager::testCustomTokenEvaluation_data() {
    QTest::addColumn<QVariantMap>("metadata");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("expected");

    QVariantMap metadata1;
    metadata1["album"] = "My Awesome Album";
    metadata1["uploader"] = "Awesome Uploader";
    metadata1["title"] = "Awesome Song";
    QTest::newRow("album and uploader") << metadata1 << "{album}/{uploader}/{title}" << "My Awesome Album/Awesome Uploader/Awesome Song";

    QVariantMap metadata2;
    metadata2["album"] = "Another Album";
    metadata2["title"] = "Another Song";
    QTest::newRow("missing uploader") << metadata2 << "{album}/{uploader}/{title}" << "Another Album/Unknown/Another Song";

    QVariantMap metadata3;
    metadata3["artist"] = "Some Artist";
    QTest::newRow("unsupported token") << metadata3 << "{artist}/{title}" << "Some Artist/Unknown";
}

void TestSortingManager::testCustomTokenEvaluation() {
    QFETCH(QVariantMap, metadata);
    QFETCH(QString, pattern);
    QFETCH(QString, expected);

    QString result = m_sortingManager->parseAndReplaceTokens(pattern, metadata);
    QCOMPARE(result, expected);
}

void TestSortingManager::testIllegalCharacterSanitization_data() {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("forward slash") << "Folder/Subfolder" << "Folder-Subfolder";
    QTest::newRow("back slash") << "Folder\\Subfolder" << "Folder-Subfolder";
    QTest::newRow("leading/trailing spaces") << "  file name  " << "file name";
    QTest::newRow("multiple spaces") << "file   name" << "file name";
    QTest::newRow("valid name") << "Valid FileName 123" << "Valid FileName 123";
}

void TestSortingManager::testIllegalCharacterSanitization() {
    QFETCH(QString, input);
    QFETCH(QString, expected);

    QString result = m_sortingManager->sanitize(input);

    qDebug() << "Input:" << input << " (len:" << input.length() << ")";
    qDebug() << "Expected:" << expected << " (len:" << expected.length() << ")";
    qDebug() << "Actual:" << result << " (len:" << result.length() << ")";
    
    QCOMPARE(result, expected);
}

void TestSortingManager::testSanitizeSpecificChars() {
    // Test each problematic character individually
    QCOMPARE(m_sortingManager->sanitize("a<b"), "a-b");
    QCOMPARE(m_sortingManager->sanitize("a>b"), "a-b");
    QCOMPARE(m_sortingManager->sanitize("a:b"), "a-b");
    QCOMPARE(m_sortingManager->sanitize("a\"b"), "a-b");
    QCOMPARE(m_sortingManager->sanitize("a/b"), "a-b");
    QCOMPARE(m_sortingManager->sanitize("a\\b"), "a-b");
    QCOMPARE(m_sortingManager->sanitize("a|b"), "a-b");
    QCOMPARE(m_sortingManager->sanitize("a?b"), "a-b");
    QCOMPARE(m_sortingManager->sanitize("a*b"), "a-b");
    // Test combinations
    QCOMPARE(m_sortingManager->sanitize("a<>b"), "a--b");
    QCOMPARE(m_sortingManager->sanitize("a:\"b"), "a--b");
    QCOMPARE(m_sortingManager->sanitize("a<>:\"/\\|?*b"), "a---------b"); // 9 chars, 9 hyphens
}


QTEST_MAIN(TestSortingManager)

#include "TestSortingManager.moc"
