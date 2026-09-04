module;
#include <cstdint>
#include <array>
#include <string>

export module caudio.db:types;

export namespace caudio::db {

struct Track {
  int64_t id{};
  std::array<uint8_t, 32> fingerprint{};
  std::string path;
  std::string title, artist, album, albumArtist, genre;
};

struct Playlist {
  int64_t id{};
  std::string name;
  std::string description;
};

struct QueueItem {
  int64_t id{};
  int64_t trackId{};
  int position{};
};

} // namespace caudio::db
