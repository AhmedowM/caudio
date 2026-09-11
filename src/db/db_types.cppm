module;
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

export module caudio.db:types;

export namespace caudio::db {

struct Track {
    int64_t id{};
    std::array<uint8_t, 32> fingerprint{};
    std::string path;
    int64_t size{};
    int64_t mtime{};
    double duration{};
    uint32_t sample_rate{};
    uint32_t channels{};
    int32_t bitrate{};
    std::string title;
    std::string artist;
    std::string album;
    std::string album_artist;
    std::string genre;
    int32_t year{};
    int32_t track_num{};
    int32_t disc_num{};
    std::string cover_art_path;
    int32_t rating{};
    int64_t play_count{};
    int64_t last_played{};
    int64_t date_added{};
    int64_t last_scanned{};
    bool dirty{false};
    int64_t deleted_at{};
    int64_t library_id{1};
};

struct Playlist {
    int64_t id{};
    std::string name;
    int32_t type{};
    std::string smart_query;
    int64_t created{};
    int64_t modified{};
    int64_t library_id{1};
};

struct QueueItem {
    int64_t id{};
    int64_t queue_id{1};
    int64_t track_id{};
    int64_t position{};
    int64_t added{};
};

struct Queue {
    int64_t id{};
    std::string name;
    int32_t repeat_mode{};
    int64_t library_id{1};
};

struct HistoryEntry {
    int64_t id{};
    int64_t track_id{};
    int64_t started_at{};
    int64_t completed_at{};
    int64_t position_ms{};
    double completion_pct{};
    int64_t queue_id{1};
};

struct Bookmark {
    int64_t id{};
    int64_t track_id{};
    int64_t position_ms{};
    std::string note;
    int64_t created{};
};

struct Library {
    int64_t id{};
    std::string path;
    std::string name;
    int64_t date_added{};
    int64_t last_scanned{};
    bool auto_scan{true};
    bool recursive{true};
    std::string extensions{"mp3,flac,ogg,wav,m4a"};
};

struct DbStats {
    int64_t num_tracks{};
    int64_t num_playlists{};
    int64_t num_queue_items{};
    int64_t num_history{};
    int64_t num_bookmarks{};
    int64_t num_libraries{};
    int64_t total_duration_ms{};
};

struct TrackQuery {
    std::string artist;
    std::string album;
    std::string genre;
    std::optional<int32_t> year{};
    std::optional<int64_t> library_id{};
    std::optional<bool> dirty{};
    int limit{};
    int offset{};
    std::string search;
};

struct HistoryQuery {
    std::optional<int64_t> track_id{};
    std::optional<int64_t> queue_id{};
    int limit{};
    int offset{};
};

} // namespace caudio::db
