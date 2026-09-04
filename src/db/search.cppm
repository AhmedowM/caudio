module;
#include <string_view>
#include <expected>
#include <vector>
#include <array>
#include <cstdint>

export module caudio.db:search;

import caudio.utils;

namespace caudio::db {

struct Track {
  int64_t id{};
  std::array<uint8_t, 32> fingerprint{};
  std::string path;
  std::string title, artist, album, albumArtist, genre;
};

struct Query {
  std::string term;       // user search term
  std::string sanitized; // fully sanitized for FTS
  int limit{100};
};

// Sanitize FTS term: add FTS5 quoting, handle special chars
std::string sanitizeFtsTerm(std::string_view term);

// FTS5 search with proper quoting + LIKE fallback
// Returns Expected<vector<Track>> with results and rank info
Expected<std::vector<Track>> searchFts(std::string_view sqlWhere, std::vector<Track> const& candidates);

// LIKE fallback search on 5 columns: title, artist, album, album_artist, genre
// Uses COLLATE NOCASE and unicode61 "remove_diacritics 2"
Expected<std::vector<Track>> searchLike(std::string_view term, std::vector<Track> const& candidates);

} // namespace caudio::db