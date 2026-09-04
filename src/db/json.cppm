module;
#include <string>
#include <vector>
#include <expected>
#include <array>
#include <cstdint>

namespace json = nlohmann;

export module caudio.db:json;

import caudio.utils;

namespace caudio::db {

struct Track {
  int64_t id{};
  std::array<uint8_t, 32> fingerprint{};
  std::string path;
  std::string title, artist, album, albumArtist, genre;
};

// Export JSON: ordered_json preserves field order for C golden parity
// Usage: json::exportTrack(track) → ordered_json string
json::ordered_json exportTrack(db::Track const& track);

// Import JSON: parse ordered_json → Track
// Returns Error if fields missing or malformed
Expected<db::Track> importTrack(json::ordered_json const& j);

} // namespace caudio::db