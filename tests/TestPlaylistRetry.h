#pragma once

#include "BaseTest.h"
#include <QtTest/QtTest>

class TestPlaylistRetry : public BaseTest
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testRetryingFailedPlaylistPlaceholderReexpandsTracks();
    void testRestoredPendingPlaylistPlaceholderIsReprobed();
    void testPlaylistLogicModesControlProbeAndExpansion();
    void testTransientPlaylistProbeFallback();
    void testSearchProbeFailureDoesNotFallBackToSingleWorker();
    void testSearchProbeUsesFlatPlaylistAndReturnsEntries();
    void testWatchUrlWithPlaylistQueryIsExpanded();
    void testExplicitPlaylistFailureClassification();
    void testEnqueueUsesPersistedPlaylistLogic();
    void testSinglePlaylistSettingQueuesOnlyFirstItem();
    void testDownloadAllCreatesOneQueueItemPerExpandedItem();
    void testPlaylistRowsAreEnqueuedInBoundedBatches();
};
