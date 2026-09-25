#pragma once
#include <string>
#include <string_view>

/**
 * @file version.hpp
 * @brief Version utilities — re-exports caudio::shortVersion constants and helpers.
 * @ingroup caudio_utils
 * @details Thin header that exposes version constants plus helpers
 * `version()` / `versionString()`. No API break — additive only.
 * Includes the CMake-generated version_config.hpp for version constants.
 */

// Include CMake-generated version constants
#include <caudio/version_config.hpp>

namespace caudio::utils {

/**
 * @brief Returns library full version (git tag, e.g. "v0.25.4").
 * @ingroup caudio_utils
 * @return String view of caudio::versionFull.
 */
inline std::string_view version() noexcept {
    return caudio::versionFull;
}

/**
 * @brief Returns library full version as owned string.
 * @ingroup caudio_utils
 * @return Copy of caudio::versionFull.
 */
inline std::string versionString() {
    return std::string(caudio::versionFull);
}

/**
 * @brief Returns short version (PROJECT_VERSION, e.g. "0.25.4").
 * @ingroup caudio_utils
 * @return String view of caudio::shortVersion.
 */
inline std::string_view shortVersion() noexcept {
    return caudio::shortVersion;
}

/**
 * @brief Returns version commit hash (short).
 * @ingroup caudio_utils
 * @return String view of caudio::versionCommit.
 */
inline std::string_view versionCommit() noexcept {
    return caudio::versionCommit;
}

/**
 * @brief Version major component.
 * @ingroup caudio_utils
 * @return versionMajor.
 */
constexpr int versionMajor() noexcept {
    return caudio::versionMajor;
}

/**
 * @brief Version minor component.
 * @ingroup caudio_utils
 */
constexpr int versionMinor() noexcept {
    return caudio::versionMinor;
}

/**
 * @brief Version patch component.
 * @ingroup caudio_utils
 */
constexpr int versionPatch() noexcept {
    return caudio::versionPatch;
}

} // namespace caudio::utils
