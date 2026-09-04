module;
#include <filesystem>
#include <span>
#include <expected>
#include <string>
#include <array>
#include <cstdint>
#include <generator>

export module caudio.db:scan;

import caudio.utils;

namespace caudio::db {

struct Track {
  int64_t id{};
  std::array<uint8_t, 32> fingerprint{};
  std::string path;
  std::string title, artist, album, albumArtist, genre;
  // ... other fields from C baseline
};

enum class ScanMode { Sampled, Full };

// BLAKE3 sampled fingerprint: 64K head + 64K tail + fileSize
// Returns Expected<Track> with computed fingerprint
Expected<Track> computeTrackFingerprint(std::filesystem::path const& path);

// Scan directory using std::filesystem::recursive_directory_iterator
// yields Track objects lazily via std::generator (C++23)
generator<const Track&> scan(std::filesystem::path const& path, ScanMode mode = ScanMode::Sampled);

} // namespace caudio::db