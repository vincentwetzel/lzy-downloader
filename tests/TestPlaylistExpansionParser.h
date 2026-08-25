#ifndef TESTPLAYLISTEXPANSIONPARSER_H
#define TESTPLAYLISTEXPANSIONPARSER_H

#include <QtTest/QtTest>
#include "BaseTest.h"

class TestPlaylistExpansionParser : public BaseTest {
    Q_OBJECT

private slots:
    void testSingleVideoItem();
    void testFlatPlaylist();
    void testNestedPlaylistEntries();
    void testMalformedData();
    void testEmptyData();
    void testRequestedIndexUsesGenericPlaylistIndex();
    void testReplayLiveStatusIsNotActive();
};

#endif // TESTPLAYLISTEXPANSIONPARSER_H
