#pragma once

#include <QString>

namespace FileReplacement {

/**
 * Moves a verified temporary file into place while preserving an existing
 * destination until the replacement has succeeded.
 */
[[nodiscard]] bool moveReplacing(const QString &sourcePath, const QString &destinationPath);

}
