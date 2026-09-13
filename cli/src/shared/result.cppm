module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module caudio.cli:result;

import caudio.utils;
import caudio.engine;
import caudio.db;

export namespace caudio::cli {

struct Status final {
    caudio::engine::PlaybackState state{caudio::engine::PlaybackState::Stopped};
    double pos{0.0};
    double dur{0.0};
    float vol{1.0f};
    bool muted{false};
    bool shuffle{false};
    caudio::engine::RepeatMode repeat{caudio::engine::RepeatMode::Off};
    int64_t track_id{0};
    std::string title{};
    std::string artist{};
    std::string path{};
    std::size_t q_size{0};
    std::size_t q_idx{0};
};

struct QueueTracks final {
    std::vector<caudio::db::Track> tracks{};
};

struct VolumeInfo final {
    float vol{1.0f};
    bool muted{false};
};

// Note: Command already defines empty LibraryStats request. Result's stats
// is named LibraryStatsData to avoid ODR collision when both partitions are
// imported via caudio.cli:shared. Alias provided for ergonomic use.
struct LibraryStatsData final {
    std::size_t tracks{0};
    std::size_t queues{0};
    std::size_t playlists{0};
};

struct LibraryStatsDetailedData final {
    std::size_t tracks{0};
    std::size_t queues{0};
    std::size_t playlists{0};
    int64_t total_duration_ms{0};
    int64_t total_play_time_ms{0};
    std::vector<caudio::db::Track> most_played{};
};

struct Tracks final {
    std::vector<caudio::db::Track> tracks{};
};

struct Playlists final {
    std::vector<caudio::db::Playlist> playlists{};
};

struct PlaylistData final {
    std::vector<caudio::db::Track> tracks{};
    std::string format{};
};

struct ConfigValue final {
    std::string key{};
    std::string value{};
};

struct ConfigValues final {
    std::vector<ConfigValue> values{};
};

struct SingleTrack final {
    caudio::db::Track track{};
};

struct TrackInfo final {
    caudio::db::Track track{};
    int64_t play_count{0};
    int64_t last_played{0};
};

struct HistoryEntry final {
    int64_t id{};
    int64_t track_id{};
    int64_t started_at{};
    int64_t completed_at{};
    int64_t position_ms{};
    double completion_pct{};
    int64_t queue_id{1};
    std::string title{};
    std::string artist{};
    std::string path{};
    double duration{};
};

struct History final {
    std::vector<HistoryEntry> entries{};
};

struct DeviceInfo final {
    std::string id{};
    std::string name{};
    bool isDefault{false};
};

struct Devices final {
    std::vector<DeviceInfo> devices{};
};

using Empty = std::monostate;
using CliError = caudio::utils::Error;

using Result = std::variant<Status, QueueTracks, VolumeInfo, LibraryStatsData, LibraryStatsDetailedData, Tracks, Playlists,
                            PlaylistData, ConfigValue, ConfigValues, SingleTrack, TrackInfo, History, Empty, CliError,
                            Devices>;

using ReplyExpected = std::expected<Result, CliError>;

} // namespace caudio::cli
