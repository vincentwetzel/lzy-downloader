#ifndef TESTDOWNLOADMANAGER_H
#define TESTDOWNLOADMANAGER_H

#include <QtTest/QtTest>
#include "BaseTest.h"
#include "core/DownloadManager.h"
#include "core/DownloadQueueManager.h"
#include "core/ConfigManager.h"
#include "core/PlaylistExpansionWorker.h"

// To access DownloadManager's private slot onPlaylistExpanded for testing
class TestableDownloadManager : public DownloadManager {
    Q_OBJECT
public:
    explicit TestableDownloadManager(ConfigManager *configManager, QObject *parent = nullptr)
        : DownloadManager(configManager, parent) {}

    void callOnPlaylistExpanded(const QString &originalUrl, const QList<QVariantMap> &expandedItems, const QString &error) {
        // Invoke the private slot through Qt's meta-object so this test stays
        // focused on the manager's classification boundary.
        int idx = metaObject()->indexOfSlot("onPlaylistExpanded(QString,QList<QVariantMap>,QString)");
        QVERIFY2(idx >= 0, "DownloadManager::onPlaylistExpanded was not registered as a slot");
        QVERIFY(metaObject()->method(idx).invoke(this,
            Q_ARG(QString, originalUrl),
            Q_ARG(QList<QVariantMap>, expandedItems),
            Q_ARG(QString, error)));
    }
};

class TestDownloadManager : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testTransientPlaylistProbeFallback();
    void testExplicitPlaylistFailureClassification();
    void testNonInteractiveDuplicateUsesFailureSignal();

private:
    DownloadManager *m_manager = nullptr;
};

#endif // TESTDOWNLOADMANAGER_H
