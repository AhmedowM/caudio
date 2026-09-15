#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <thread>

// Import utils for SpscRing used by makeDummyRing (test-only, no AudioOutput device).
// This import is safe to repeat in TUs that also `import caudio.utils;`.
import caudio.utils;

/**
 * @file common.hpp
 * @brief Shared test helpers (temp paths, busy-wait, NOAUDIO switch).
 * @ingroup caudio_test
 */

/// @brief Test-only helpers for headless/CI audio control.
namespace caudio::test {

/**
 * @brief Returns true when audio device tests should be skipped.
 * @details Checks env `CAUDIO_TEST_NOAUDIO` for values `1`, `true`, `True`.
 * When true, tests that would call `AudioOutput::create` / `ma_device_start`
 * and beep should `SKIP` instead. Set in CI via `CAUDIO_TEST_NOAUDIO=1`.
 * @return true if `CAUDIO_TEST_NOAUDIO` is `1`/`true`/`True`.
 */
inline bool noAudio() noexcept {
    const char* v = std::getenv("CAUDIO_TEST_NOAUDIO");
    return v && (std::string(v) == "1" || std::string(v) == "true" || std::string(v) == "True");
}

/**
 * @brief Creates a dummy SPSC ring without touching the audio device.
 * @param sampleRate Sample rate hint (unused, for API symmetry with AudioOutput::Config).
 * @param channels Channel count for the ring (default 2).
 * @param frames Capacity in frames (default 8192).
 * @return `caudio::utils::SpscRing<float>` ready for `fillForTest` exercises.
 * @details Test-only helper — does NOT call `AudioOutput::create` or
 * `ma_device_init`/`ma_device_start`, so no beep. Example:
 * `auto ring = caudio::test::makeDummyRing(48000, 2);`
 */
inline caudio::utils::SpscRing<float> makeDummyRing(uint32_t sampleRate = 48000, uint32_t channels = 2,
                                                     size_t frames = 8192) {
    (void)sampleRate;
    return caudio::utils::SpscRing<float>{frames, channels};
}

} // namespace caudio::test

/**
 * @brief Catch2 helper: skip current test when `CAUDIO_TEST_NOAUDIO=1`.
 * @details Usage: `CAUDIO_SKIP_IF_NOAUDIO();` at top of a TEST_CASE that
 * would otherwise beep via `AudioOutput::create`. Expands to `SKIP(...)`.
 */
#define CAUDIO_SKIP_IF_NOAUDIO()                                                                   \
    do {                                                                                           \
        if (caudio::test::noAudio()) {                                                             \
            SKIP("CAUDIO_TEST_NOAUDIO=1 - audio device test skipped (no beep)");                  \
        }                                                                                          \
    } while (0)

namespace caudio::test_helpers {

inline std::filesystem::path tempDbPath(const std::string &prefix) {
    static std::atomic<int> ctr{0};
    auto dir = std::filesystem::temp_directory_path();
    std::string name = prefix + "_" + std::to_string(ctr.fetch_add(1)) + "_" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db";
    return dir / name;
}

inline std::filesystem::path tempDirPath(const std::string &prefix) {
    static std::atomic<int> ctr2{1000};
    auto dir = std::filesystem::temp_directory_path();
    std::string name = prefix + "_" + std::to_string(ctr2.fetch_add(1));
    auto p = dir / name;
    std::filesystem::create_directories(p);
    return p;
}

inline void busyWaitUntil(std::function<bool()> pred, std::chrono::milliseconds timeout = std::chrono::milliseconds{2000},
                          std::chrono::milliseconds interval = std::chrono::milliseconds{10}) {
    auto start = std::chrono::steady_clock::now();
    while (!pred()) {
        if (std::chrono::steady_clock::now() - start > timeout) break;
        std::this_thread::sleep_for(interval);
    }
}

inline void safeRemoveDb(const std::string& p) {
    std::error_code ec;
    std::filesystem::remove(p, ec);
    std::filesystem::remove(p + "-wal", ec);
    std::filesystem::remove(p + "-shm", ec);
}

} // namespace caudio::test_helpers