#include "TestSortingManager.h"
#include <QVariantMap>

void TestSortingManager::testCustomTokenEvaluation_data() {
    QTest::addColumn<QVariantMap>("metadata");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("expected");

    QVariantMap metadata1;
    metadata1[QStringLiteral("album")] = QStringLiteral("My Awesome Album");
    metadata1[QStringLiteral("uploader")] = QStringLiteral("Awesome Uploader");
    metadata1[QStringLiteral("title")] = QStringLiteral("Awesome Song");
    QTest::newRow("album and uploader") << metadata1 << QStringLiteral("{album}/{uploader}/{title}") << QStringLiteral("My Awesome Album/Awesome Uploader/Awesome Song");

    QVariantMap metadata2;
    metadata2[QStringLiteral("album")] = QStringLiteral("Another Album");
    metadata2[QStringLiteral("title")] = QStringLiteral("Another Song");
    QTest::newRow("missing uploader") << metadata2 << QStringLiteral("{album}/{uploader}/{title}") << QStringLiteral("Another Album/Unknown/Another Song");

    QVariantMap metadata3;
    metadata3[QStringLiteral("artist")] = QStringLiteral("Some Artist");
    QTest::newRow("unsupported token") << metadata3 << QStringLiteral("{artist}/{title}") << QStringLiteral("Some Artist/Unknown");
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

    QTest::newRow("forward slash") << QStringLiteral("Folder/Subfolder") << QStringLiteral("Folder-Subfolder");
    QTest::newRow("back slash") << QStringLiteral("Folder\\Subfolder") << QStringLiteral("Folder-Subfolder");
    QTest::newRow("leading/trailing spaces") << QStringLiteral("  file name  ") << QStringLiteral("file name");
    QTest::newRow("multiple spaces") << QStringLiteral("file   name") << QStringLiteral("file name");
    QTest::newRow("valid name") << QStringLiteral("Valid FileName 123") << QStringLiteral("Valid FileName 123");
}

void TestSortingManager::testIllegalCharacterSanitization() {
    QFETCH(QString, input);
    QFETCH(QString, expected);

    QString result = m_sortingManager->sanitize(input);

    QCOMPARE(result, expected);
}

void TestSortingManager::testSanitizeSpecificChars() {
    // Test each problematic character individually
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a<b")), QStringLiteral("a-b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a>b")), QStringLiteral("a-b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a:b")), QStringLiteral("a-b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a\"b")), QStringLiteral("a-b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a/b")), QStringLiteral("a-b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a\\b")), QStringLiteral("a-b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a|b")), QStringLiteral("a-b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a?b")), QStringLiteral("a-b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a*b")), QStringLiteral("a-b"));
    // Test combinations
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a<>b")), QStringLiteral("a--b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a:\"b")), QStringLiteral("a--b"));
    QCOMPARE(m_sortingManager->sanitize(QStringLiteral("a<>:\"/\\|?*b")), QStringLiteral("a---------b")); // 9 chars, 9 hyphens
}


QTEST_MAIN(TestSortingManager)
