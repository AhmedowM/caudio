module;
#include <string>
#include <string_view>

#include "caudio/version.hpp"

/**
 * @file version.cppm
 * @brief Version utilities — re-exports caudio::kVersion constants and helpers.
 * @ingroup caudio_utils
 * @details Thin re-export partition `caudio.utils:version` that includes the
 * generated `caudio/version.hpp` and exposes version constants plus helpers
 * `version()` / `versionString()`. No API break — additive only.
 */

export module caudio.utils:version;

export namespace caudio::utils {

/**
 * @brief Returns library full version (git tag, e.g. "v0.25.4").
 * @ingroup caudio_utils
 * @return String view of caudio::kVersionFull.
 */
inline std::string_view version() noexcept {
    return caudio::kVersionFull;
}

/**
 * @brief Returns library full version as owned string.
 * @ingroup caudio_utils
 * @return Copy of caudio::kVersionFull.
 */
inline std::string versionString() {
    return std::string(caudio::kVersionFull);
}

/**
 * @brief Returns short version (PROJECT_VERSION, e.g. "0.25.4").
 * @ingroup caudio_utils
 * @return String view of caudio::kVersion.
 */
inline std::string_view shortVersion() noexcept {
    return caudio::kVersion;
}

/**
 * @brief Returns version commit hash (short).
 * @ingroup caudio_utils
 * @return String view of caudio::kVersionCommit.
 */
inline std::string_view versionCommit() noexcept {
    return caudio::kVersionCommit;
}

/**
 * @brief Version major component.
 * @ingroup caudio_utils
 * @return kVersionMajor.
 */
constexpr int versionMajor() noexcept {
    return caudio::kVersionMajor;
}

/**
 * @brief Version minor component.
 * @ingroup caudio_utils
 */
constexpr int versionMinor() noexcept {
    return caudio::kVersionMinor;
}

/**
 * @brief Version patch component.
 * @ingroup caudio_utils
 */
constexpr int versionPatch() noexcept {
    return caudio::kVersionPatch;
}

} // namespace caudio::utils

// Re-export top-level constants for convenience via caudio::utils::kVersion etc.
export namespace caudio::utils {
inline constexpr const char* kVersion = caudio::kVersion;
inline constexpr const char* kVersionFull = caudio::kVersionFull;
inline constexpr const char* kVersionCommit = caudio::kVersionCommit;
} // namespace caudio::utils
