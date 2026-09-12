#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <thread>

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

} // namespace caudio::test_helpers
