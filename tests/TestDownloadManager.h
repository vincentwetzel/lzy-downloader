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
        : DownloadManager(configManager, parent), m_configManager(configManager) {}

    void emitPlaylistExpansion(const QString &url, const QVariantMap &options,
                               const QList<QVariantMap> &items) {
        auto *worker = new PlaylistExpansionWorker(url, m_configManager, this);
        worker->setProperty("options", options);
        QObject::connect(worker,
                         SIGNAL(expansionFinished(QString,QList<QVariantMap>,QString)),
                         this,
                         SLOT(onPlaylistExpanded(QString,QList<QVariantMap>,QString)));
        emit worker->expansionFinished(url, items, QString());
    }

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

    void callOnFinalizationComplete(const QString &id, bool success, const QString &message) {
        const int idx = metaObject()->indexOfSlot("onFinalizationComplete(QString,bool,QString)");
        QVERIFY2(idx >= 0, "DownloadManager::onFinalizationComplete was not registered as a slot");
        QVERIFY(metaObject()->method(idx).invoke(this,
            Q_ARG(QString, id), Q_ARG(bool, success), Q_ARG(QString, message)));
    }

private:
    ConfigManager *m_configManager;
};

class TestDownloadManager : public BaseTest {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testTransientPlaylistProbeFallback();
    void testExplicitPlaylistFailureClassification();
    void testEnqueueUsesPersistedPlaylistLogic();
    void testSinglePlaylistSettingQueuesOnlyFirstItem();
    void testNonInteractiveDuplicateUsesFailureSignal();
    void testSlowProbeFallbackAndExplicitPlaylistSmoke();
    void testDownloadWorkerUsesDedicatedThread();
    void testMetadataEmbedderRunsOffGuiThread();
    void testMetadataEmbedderSkipsMissingThumbnailWithoutOtherWork();
    void testFinalizationDoesNotBlockGuiThread();
    void testCompletionStateSaveDoesNotBlockGuiThread();

private:
    DownloadManager *m_manager = nullptr;
};

#endif // TESTDOWNLOADMANAGER_H
