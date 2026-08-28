#pragma once

#include <QtTest/QtTest>

class TestBrowserCookieFile : public QObject {
    Q_OBJECT

private slots:
    void createsScopedNetscapeFile();
    void rejectsCookieOutsideUrlScope();
    void rejectsControlCharacters();
    void rejectsCookieOutsideUrlPath();
    void rejectsSecureCookieOnHttp();
    void rejectsInvalidExpiration();
    void removesExpiredOwnedFile();
};
