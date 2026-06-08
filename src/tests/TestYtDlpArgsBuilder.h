#pragma once

#include "BaseTest.h"
#include <QtTest/QtTest>

class TestYtDlpArgsBuilder : public BaseTest { // Inherit from BaseTest
    Q_OBJECT

private slots:
    void testBasicVideoArguments();
    void testSponsorBlockArguments();
    void testLivestreamArguments();
    void testPostLiveReplayUsesVideoArguments();
    void testLiveUrlHintBypassesAria2ForVideoArguments();
    void testAudioThumbnailEmbedding();
    void testAudioPlaylistFolderJpg();
};
