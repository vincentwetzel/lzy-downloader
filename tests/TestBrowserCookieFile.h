#pragma once

#include <QtTest/QtTest>

class TestBrowserCookieFile : public QObject {
    Q_OBJECT

private slots:
    void createsScopedNetscapeFile();
    void rejectsCookieOutsideUrlScope();
    void rejectsControlCharacters();
};
