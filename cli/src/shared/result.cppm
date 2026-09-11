module;
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <expected>
#include <utility>

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
    int64_t trackId{0};
    std::string title{};
    std::string artist{};
    std::string path{};
    std::size_t qSize{0};
    std::size_t qIdx{0};
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

struct Tracks final {
    std::vector<caudio::db::Track> tracks{};
};

struct Playlists final {
    std::vector<caudio::db::Playlist> playlists{};
};

struct ConfigValue final {
    std::string key{};
    std::string value{};
};

struct ConfigValues final {
    std::vector<ConfigValue> values{};
};

using Empty = std::monostate;
using CliError = caudio::utils::Error;

using Result = std::variant<Status, QueueTracks, VolumeInfo, LibraryStatsData, Tracks, Playlists, ConfigValue, ConfigValues, Empty, CliError>;

using ReplyExpected = std::expected<Result, CliError>;

} // namespace caudio::cli
