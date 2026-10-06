#pragma once
#include <QString>

namespace UrlUtils {
    /** Extracts an HTTP(S) URL; returns trimmed input when no link is found. */
    QString extractUrl(const QString &text);
}
