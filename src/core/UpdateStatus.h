#pragma once

#include <cstdint>

namespace Updater {

/**
 * @brief Represents the result of a remote binary or application update check.
 */
enum class UpdateStatus : uint8_t
{
    UpdateAvailable, ///< A newer version was found and successfully downloaded or is ready to install.
    UpToDate,        ///< The local version matches or is newer than the remote release.
    Error            ///< The update check failed due to a network, parsing, or filesystem error.
};
} // namespace Updater