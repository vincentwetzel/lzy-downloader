#pragma once

#include "BaseTest.h"
#include <QtTest/QtTest>

class TestYtDlpArgsBuilder : public BaseTest { // Inherit from BaseTest
    Q_OBJECT

private slots:
    void testBasicVideoArguments();
    void testRateLimitUsesSavedSettingAndRequestOverride();
    void testPlaylistLogicArguments();
    void testPlaylistExpansionUsesConfiguredBrowserCookies();
    void testSponsorBlockArguments();
    void testLivestreamArguments();
    void testPostLiveReplayUsesVideoArguments();
    void testLivePathIsNotLivestreamEvidence();
    void testAria2RetryPolicyArguments();
    void testAudioThumbnailEmbedding();
    void testUnsupportedOpusThumbnailEmbedding();
    void testAudioPlaylistFolderJpg();
    void testAudioPlaylistArtistMetadataFallback();
    void testAudioPlaylistIndexBecomesTrackMetadata();
    void testOrphanedTemporaryDirectorySweep();
    void testTemporaryDirectoryOwnershipGuard();
};
