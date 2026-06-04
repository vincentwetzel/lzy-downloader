#include "BaseTest.h"
#include "core/PlaylistRangeDialog.h"

#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVariantMap>
#include <QtTest/QtTest>

class TestPlaylistRangeDialog : public BaseTest {
    Q_OBJECT

private slots:
    void testInitialState();
    void testSelectNoneAndAll();
    void testTextToListSync();
    void testListToTextSync();
    void testGetSelectedItems();
};

void TestPlaylistRangeDialog::testInitialState() {
    QList<QVariantMap> items;
    for (int i = 0; i < 5; ++i) {
        QVariantMap item;
        item[QStringLiteral("title")] = QStringLiteral("Item %1").arg(i + 1);
        items.append(item);
    }

    PlaylistRangeDialog dialog(items);
    
    QListWidget *listWidget = dialog.findChild<QListWidget*>();
    QVERIFY(listWidget != nullptr);
    QCOMPARE(listWidget->count(), 5);
    
    // By default, all items should be checked
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(listWidget->item(i)->checkState(), Qt::Checked);
    }
    
    // The range text edit should reflect the initial all-checked state
    QLineEdit *rangeEdit = dialog.findChild<QLineEdit*>();
    QVERIFY(rangeEdit != nullptr);
    QCOMPARE(rangeEdit->text(), QStringLiteral("1-5"));
}

void TestPlaylistRangeDialog::testSelectNoneAndAll() {
    QList<QVariantMap> items;
    for (int i = 0; i < 3; ++i) {
        QVariantMap item;
        item[QStringLiteral("title")] = QStringLiteral("Item %1").arg(i + 1);
        items.append(item);
    }

    PlaylistRangeDialog dialog(items);
    QListWidget *listWidget = dialog.findChild<QListWidget*>();
    QLineEdit *rangeEdit = dialog.findChild<QLineEdit*>();
    QVERIFY(listWidget != nullptr);
    QVERIFY(rangeEdit != nullptr);
    
    QList<QPushButton*> buttons = dialog.findChildren<QPushButton*>();
    QPushButton *selectNoneBtn = nullptr;
    QPushButton *selectAllBtn = nullptr;
    for (QPushButton *btn : buttons) {
        if (btn->text() == tr("Select None")) selectNoneBtn = btn;
        if (btn->text() == tr("Select All")) selectAllBtn = btn;
    }
    
    QVERIFY(selectNoneBtn != nullptr);
    QVERIFY(selectAllBtn != nullptr);
    
    // Test Select None
    selectNoneBtn->click();
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(listWidget->item(i)->checkState(), Qt::Unchecked);
    }
    QCOMPARE(rangeEdit->text(), QStringLiteral(""));
    
    // Test Select All
    selectAllBtn->click();
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(listWidget->item(i)->checkState(), Qt::Checked);
    }
    QCOMPARE(rangeEdit->text(), QStringLiteral("1-3"));
}

void TestPlaylistRangeDialog::testTextToListSync() {
    QList<QVariantMap> items;
    for (int i = 0; i < 5; ++i) {
        QVariantMap item;
        item[QStringLiteral("title")] = QStringLiteral("Item %1").arg(i + 1);
        items.append(item);
    }

    PlaylistRangeDialog dialog(items);
    QListWidget *listWidget = dialog.findChild<QListWidget*>();
    QVERIFY(listWidget != nullptr);
    
    // Simulate user typing distinct list numbers: "1, 3, 5"
    QMetaObject::invokeMethod(&dialog, "onRangeTextChanged", Q_ARG(QString, QStringLiteral("1, 3, 5")));
    QCOMPARE(listWidget->item(0)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(1)->checkState(), Qt::Unchecked);
    QCOMPARE(listWidget->item(2)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(3)->checkState(), Qt::Unchecked);
    QCOMPARE(listWidget->item(4)->checkState(), Qt::Checked);
    
    // Simulate user typing a range: "2-4"
    QMetaObject::invokeMethod(&dialog, "onRangeTextChanged", Q_ARG(QString, QStringLiteral("2-4")));
    QCOMPARE(listWidget->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(listWidget->item(1)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(2)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(3)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(4)->checkState(), Qt::Unchecked);

    // Simulate user typing an open-ended range: "1-" (should select from 1 to the end)
    QMetaObject::invokeMethod(&dialog, "onRangeTextChanged", Q_ARG(QString, QStringLiteral("1-")));
    QCOMPARE(listWidget->item(0)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(1)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(2)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(3)->checkState(), Qt::Checked);
    QCOMPARE(listWidget->item(4)->checkState(), Qt::Checked);
}

void TestPlaylistRangeDialog::testListToTextSync() {
    QList<QVariantMap> items;
    for (int i = 0; i < 5; ++i) {
        QVariantMap item;
        item[QStringLiteral("title")] = QStringLiteral("Item %1").arg(i + 1);
        items.append(item);
    }

    PlaylistRangeDialog dialog(items);
    QListWidget *listWidget = dialog.findChild<QListWidget*>();
    QLineEdit *rangeEdit = dialog.findChild<QLineEdit*>();
    QVERIFY(listWidget != nullptr);
    QVERIFY(rangeEdit != nullptr);
    
    listWidget->item(1)->setCheckState(Qt::Unchecked);
    QCOMPARE(rangeEdit->text(), QStringLiteral("1, 3-5"));
    
    listWidget->item(3)->setCheckState(Qt::Unchecked);
    QCOMPARE(rangeEdit->text(), QStringLiteral("1, 3, 5"));
}

void TestPlaylistRangeDialog::testGetSelectedItems() {
    QList<QVariantMap> items;
    for (int i = 0; i < 5; ++i) {
        QVariantMap item;
        item[QStringLiteral("id")] = i;
        items.append(item);
    }

    PlaylistRangeDialog dialog(items);
    QListWidget *listWidget = dialog.findChild<QListWidget*>();
    QVERIFY(listWidget != nullptr);
    listWidget->item(1)->setCheckState(Qt::Unchecked);
    listWidget->item(3)->setCheckState(Qt::Unchecked);
    QCOMPARE(static_cast<int>(dialog.getSelectedItems().size()), 3);
}

QTEST_MAIN(TestPlaylistRangeDialog)
#include "TestPlaylistRangeDialog.moc"