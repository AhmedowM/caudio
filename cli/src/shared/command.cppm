module;
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

export module caudio.cli:command;

import caudio.engine;

export namespace caudio::cli {

struct Play final {};
struct Pause final {};
struct Resume final {};
struct Restart final {};
struct Stop final {};
struct Next final {};
struct Prev final {};

struct Seek final {
    double seconds{0.0};
};

struct StatusReq final {};

struct VolumeSet final {
    std::optional<float> level{};
    std::optional<bool> mute{};
    std::optional<int> deltaPct{};
};

struct QueueList final {};
struct QueueQueues final {};

struct QueueSwitch final {
    int64_t qid{1};
};

struct QueueAdd final {
    std::string query{};
    bool search{false};
};

struct QueueRemove final {
    std::string idOrIndex{};
};

struct QueueMove final {
    std::size_t from{0};
    std::size_t to{0};
};

struct QueueClear final {};

struct QueueShuffle final {
    std::optional<bool> on{};
};

struct QueueRepeat final {
    std::optional<caudio::engine::RepeatMode> mode{};
};

struct PlaylistList final {};

struct PlaylistTracks final {
    int64_t pid{0};
};

struct PlaylistLoad final {
    int64_t pid{0};
    bool play{false};
};

struct PlaylistSave final {
    std::string name{};
    std::optional<int64_t> queue_id{};
};

struct PlaylistDelete final {
    int64_t pid{0};
};

struct LibraryScan final {
    std::optional<std::string> path{};
    std::string mode{"sampled"};
};

struct LibrarySearch final {
    std::string query{};
    int limit{50};
};

struct LibraryStats final {};

struct ConfigGet final {
    std::string key{};
};

struct ConfigSet final {
    std::string key{};
    std::string value{};
};

struct ConfigList final {};

struct ConfigExport final {
    std::string path{};
};

struct ConfigImport final {
    std::string path{};
};

struct Shutdown final {};

struct Preview final {
    std::string file{};
};

using Command = std::variant<
    Play, Pause, Resume, Restart, Stop, Next, Prev, Seek, StatusReq,
    VolumeSet, QueueList, QueueQueues, QueueSwitch, QueueAdd, QueueRemove,
    QueueMove, QueueClear, QueueShuffle, QueueRepeat,
    PlaylistList, PlaylistTracks, PlaylistLoad, PlaylistSave, PlaylistDelete,
    LibraryScan, LibrarySearch, LibraryStats, ConfigGet, ConfigSet, ConfigList,
    ConfigExport, ConfigImport, Shutdown, Preview>;

// helper concepts
template <typename T>
concept CommandAlternative = requires {
    requires std::disjunction_v<
        std::is_same<T, Play>, std::is_same<T, Pause>, std::is_same<T, Resume>,
        std::is_same<T, Restart>, std::is_same<T, Stop>, std::is_same<T, Next>,
        std::is_same<T, Prev>, std::is_same<T, Seek>, std::is_same<T, StatusReq>,
        std::is_same<T, VolumeSet>, std::is_same<T, QueueList>,
        std::is_same<T, QueueQueues>, std::is_same<T, QueueSwitch>,
        std::is_same<T, QueueAdd>, std::is_same<T, QueueRemove>,
        std::is_same<T, QueueMove>, std::is_same<T, QueueClear>,
        std::is_same<T, QueueShuffle>, std::is_same<T, QueueRepeat>,
        std::is_same<T, PlaylistList>, std::is_same<T, PlaylistTracks>,
        std::is_same<T, PlaylistLoad>, std::is_same<T, PlaylistSave>,
        std::is_same<T, PlaylistDelete>, std::is_same<T, LibraryScan>,
        std::is_same<T, LibrarySearch>, std::is_same<T, LibraryStats>,
        std::is_same<T, ConfigGet>, std::is_same<T, ConfigSet>,
        std::is_same<T, ConfigList>, std::is_same<T, ConfigExport>,
        std::is_same<T, ConfigImport>, std::is_same<T, Shutdown>,
        std::is_same<T, Preview>>;
};

template <typename T>
concept CommandType = CommandAlternative<std::remove_cvref_t<T>>;

} // namespace caudio::cli




