/**
 * @file result.cppm
 * @brief CLI result types and variants for the caudio IPC protocol.
 * @ingroup caudio_cli
 *
 * Defines all result structs that can be returned from the caudio service
 * daemon to a client. Results are serialized as JSON over the IPC
 * socket/named pipe connection.
 *
 * ## IPC Protocol Structure
 *
 * Results are sent as JSON objects with a "type" field identifying the result type.
 * The full IPC message format is:
 * - 4-byte big-endian length prefix (uint32_t)
 * - JSON payload containing an IpcReply with:
 *   - `id`: uint32_t request identifier (matches request)
 *   - `ok`: boolean indicating success
 *   - `result`: Result object (if ok=true) or `error`: Error object (if ok=false)
 *
 * @see caudio::cli::protocol for serialization/deserialization functions
 * @see caudio::cli::Command for request types
 */

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

// ---------------------------------------------------------------------------
// Playback Status Result
// ---------------------------------------------------------------------------

/**
 * @struct Status
 * @brief Current playback status returned by StatusReq and most mutating commands.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Status", "state": "Playing", "pos": 120.5, "dur": 240.0, "vol": 0.8, "muted": false, "shuffle": true, "repeat": "Off", "track_id": 123, "title": "Song", "artist": "Artist", "path": "/music/song.mp3", "q_size": 10, "q_idx": 0}
 *
 * @param state Current playback state.
 * @param pos Current playback position in seconds.
 * @param dur Total duration of current track in seconds.
 * @param vol Current volume level (0.0 to 1.0).
 * @param muted Whether playback is muted.
 * @param shuffle Whether shuffle mode is enabled.
 * @param repeat Current repeat mode.
 * @param track_id ID of the currently playing track (0 if none).
 * @param title Title of the current track.
 * @param artist Artist of the current track.
 * @param path File path of the current track.
 * @param q_size Number of tracks in the active queue.
 * @param q_idx Index of the current track in the queue (0-based).
 */
struct Status final {
    /**
     * @brief Current playback state.
     */
    caudio::engine::PlaybackState state{caudio::engine::PlaybackState::Stopped};
    /**
     * @brief Current playback position in seconds.
     */
    double pos{0.0};
    /**
     * @brief Total duration of current track in seconds.
     */
    double dur{0.0};
    /**
     * @brief Current volume level (0.0 to 1.0).
     */
    float vol{1.0f};
    /**
     * @brief Whether playback is muted.
     */
    bool muted{false};
    /**
     * @brief Whether shuffle mode is enabled.
     */
    bool shuffle{false};
    /**
     * @brief Current repeat mode.
     */
    caudio::engine::RepeatMode repeat{caudio::engine::RepeatMode::Off};
    /**
     * @brief ID of the currently playing track (0 if none).
     */
    int64_t track_id{0};
    /**
     * @brief Title of the current track.
     */
    std::string title{};
    /**
     * @brief Artist of the current track.
     */
    std::string artist{};
    /**
     * @brief File path of the current track.
     */
    std::string path{};
    /**
     * @brief Number of tracks in the active queue.
     */
    std::size_t q_size{0};
    /**
     * @brief Index of the current track in the queue (0-based).
     */
    std::size_t q_idx{0};
    /**
     * @brief Daemon/library version (full git tag, e.g. "v0.25.4").
     * @details Populated by service_detail::buildStatus from caudio::utils::kVersionFull.
     * Default is kVersionFull so local builds without daemon still show version.
     */
    std::string version{caudio::utils::kVersionFull};
};

// ---------------------------------------------------------------------------
// Queue Results
// ---------------------------------------------------------------------------

/**
 * @struct QueueTracks
 * @brief List of tracks in a queue (returned by QueueList, QueueAdd, QueueRemove, etc.).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueTracks", "tracks": [{"id": 1, "title": "Song", ...}]}
 *
 * @param tracks Vector of tracks in the queue.
 */
struct QueueTracks final {
    /**
     * @brief Vector of tracks in the queue.
     */
    std::vector<caudio::db::Track> tracks{};
};

// ---------------------------------------------------------------------------
// Volume Results
// ---------------------------------------------------------------------------

/**
 * @struct VolumeInfo
 * @brief Current volume level and mute state (returned by VolumeSet).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "VolumeInfo", "vol": 0.8, "muted": false}
 *
 * @param vol Current volume level (0.0 to 1.0).
 * @param muted Whether playback is muted.
 */
struct VolumeInfo final {
    /**
     * @brief Current volume level (0.0 to 1.0).
     */
    float vol{1.0f};
    /**
     * @brief Whether playback is muted.
     */
    bool muted{false};
};

// ---------------------------------------------------------------------------
// Library Statistics Results
// ---------------------------------------------------------------------------

// Note: Command already defines empty LibraryStats request. Result's stats
// is named LibraryStatsData to avoid ODR collision when both partitions are
// imported via caudio.cli:shared. Alias provided for ergonomic use.

/**
 * @struct LibraryStatsData
 * @brief Basic library statistics (returned by LibraryStats and QueueQueues).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibraryStats", "tracks": 1000, "queues": 5, "playlists": 20}
 *
 * @param tracks Total number of tracks in the library.
 * @param queues Total number of queues.
 * @param playlists Total number of playlists.
 */
struct LibraryStatsData final {
    /**
     * @brief Total number of tracks in the library.
     */
    std::size_t tracks{0};
    /**
     * @brief Total number of queues.
     */
    std::size_t queues{0};
    /**
     * @brief Total number of playlists.
     */
    std::size_t playlists{0};
};

/**
 * @struct LibraryStatsDetailedData
 * @brief Detailed library statistics including play history (returned by LibraryStatsDetailed).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibraryStatsDetailed", "tracks": 1000, "queues": 5, "playlists": 20, "total_duration_ms": 36000000, "total_play_time_ms": 7200000, "most_played": [...]}
 *
 * @param tracks Total number of tracks in the library.
 * @param queues Total number of queues.
 * @param playlists Total number of playlists.
 * @param total_duration_ms Total duration of all tracks in milliseconds.
 * @param total_play_time_ms Total play time across all tracks in milliseconds.
 * @param most_played Most played tracks (up to 10).
 */
struct LibraryStatsDetailedData final {
    /**
     * @brief Total number of tracks in the library.
     */
    std::size_t tracks{0};
    /**
     * @brief Total number of queues.
     */
    std::size_t queues{0};
    /**
     * @brief Total number of playlists.
     */
    std::size_t playlists{0};
    /**
     * @brief Total duration of all tracks in milliseconds.
     */
    int64_t total_duration_ms{0};
    /**
     * @brief Total play time across all tracks in milliseconds.
     */
    int64_t total_play_time_ms{0};
    /**
     * @brief Most played tracks (up to 10).
     */
    std::vector<caudio::db::Track> most_played{};
};

// ---------------------------------------------------------------------------
// Track and Playlist Results
// ---------------------------------------------------------------------------

/**
 * @struct Tracks
 * @brief List of tracks (returned by LibrarySearch, LibraryList).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Tracks", "tracks": [{"id": 1, "title": "Song", ...}]}
 *
 * @param tracks Vector of tracks.
 */
struct Tracks final {
    /**
     * @brief Vector of tracks.
     */
    std::vector<caudio::db::Track> tracks{};
};

/**
 * @struct Playlists
 * @brief List of playlists (returned by PlaylistList).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Playlists", "playlists": [{"id": 1, "name": "My Playlist", ...}]}
 *
 * @param playlists Vector of playlists.
 */
struct Playlists final {
    /**
     * @brief Vector of playlists.
     */
    std::vector<caudio::db::Playlist> playlists{};
};

/**
 * @struct PlaylistData
 * @brief Playlist contents with format info (returned by PlaylistExport, PlaylistTracks).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistData", "tracks": [...], "format": "m3u"}
 *
 * @param tracks Tracks in the playlist.
 * @param format Export format (e.g., "m3u").
 */
struct PlaylistData final {
    /**
     * @brief Tracks in the playlist.
     */
    std::vector<caudio::db::Track> tracks{};
    /**
     * @brief Export format (e.g., "m3u").
     */
    std::string format{};
};

// ---------------------------------------------------------------------------
// Config Results
// ---------------------------------------------------------------------------

/**
 * @struct ConfigValue
 * @brief Single configuration key-value pair (returned by ConfigGet).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "ConfigValue", "key": "volume", "value": "0.8"}
 *
 * @param key Configuration key.
 * @param value Configuration value.
 */
struct ConfigValue final {
    /**
     * @brief Configuration key.
     */
    std::string key{};
    /**
     * @brief Configuration value.
     */
    std::string value{};
};

/**
 * @struct ConfigValues
 * @brief Multiple configuration key-value pairs (returned by ConfigList).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "ConfigValues", "values": [{"key": "volume", "value": "0.8"}, ...]}
 *
 * @param values Vector of configuration values.
 */
struct ConfigValues final {
    /**
     * @brief Vector of configuration values.
     */
    std::vector<ConfigValue> values{};
};

// ---------------------------------------------------------------------------
// Track Detail Results
// ---------------------------------------------------------------------------

/**
 * @struct SingleTrack
 * @brief Single track with full metadata (returned by TagGet, LibrarySearch single result).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "SingleTrack", "track": {"id": 1, "title": "Song", ...}}
 *
 * @param track Track metadata.
 */
struct SingleTrack final {
    /**
     * @brief Track metadata.
     */
    caudio::db::Track track{};
};

/**
 * @struct TrackInfo
 * @brief Track with play statistics (returned by Info command).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "TrackInfo", "track": {...}, "play_count": 42, "last_played": 1699999999000}
 *
 * @param track Track metadata.
 * @param play_count Number of times this track has been played.
 * @param last_played Timestamp of last play (milliseconds since epoch, 0 if never).
 */
struct TrackInfo final {
    /**
     * @brief Track metadata.
     */
    caudio::db::Track track{};
    /**
     * @brief Number of times this track has been played.
     */
    int64_t play_count{0};
    /**
     * @brief Timestamp of last play (milliseconds since epoch, 0 if never).
     */
    int64_t last_played{0};
};

// ---------------------------------------------------------------------------
// History Results
// ---------------------------------------------------------------------------

/**
 * @struct HistoryEntry
 * @brief Single playback history entry.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "HistoryEntry", "id": 1, "track_id": 123, "started_at": 1699999999000, "completed_at": 1699999999240, "position_ms": 240000, "completion_pct": 100.0, "queue_id": 1, "title": "Song", "artist": "Artist", "path": "/music/song.mp3", "duration": 240.0}
 *
 * @param id History entry ID.
 * @param track_id Track ID that was played.
 * @param started_at Playback start timestamp (milliseconds since epoch).
 * @param completed_at Playback completion timestamp (milliseconds since epoch).
 * @param position_ms Playback position at completion in milliseconds.
 * @param completion_pct Completion percentage (0.0 to 100.0).
 * @param queue_id Queue ID the track was played from.
 * @param title Track title (cached for display).
 * @param artist Track artist (cached for display).
 * @param path Track file path (cached for display).
 * @param duration Track duration in seconds.
 */
struct HistoryEntry final {
    /**
     * @brief History entry ID.
     */
    int64_t id{};
    /**
     * @brief Track ID that was played.
     */
    int64_t track_id{};
    /**
     * @brief Playback start timestamp (milliseconds since epoch).
     */
    int64_t started_at{};
    /**
     * @brief Playback completion timestamp (milliseconds since epoch).
     */
    int64_t completed_at{};
    /**
     * @brief Playback position at completion in milliseconds.
     */
    int64_t position_ms{};
    /**
     * @brief Completion percentage (0.0 to 100.0).
     */
    double completion_pct{};
    /**
     * @brief Queue ID the track was played from.
     */
    int64_t queue_id{1};
    /**
     * @brief Track title (cached for display).
     */
    std::string title{};
    /**
     * @brief Track artist (cached for display).
     */
    std::string artist{};
    /**
     * @brief Track file path (cached for display).
     */
    std::string path{};
    /**
     * @brief Track duration in seconds.
     */
    double duration{};
};

/**
 * @struct History
 * @brief List of playback history entries (returned by HistoryList).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "History", "entries": [...]}
 *
 * @param entries Vector of history entries.
 */
struct History final {
    /**
     * @brief Vector of history entries.
     */
    std::vector<HistoryEntry> entries{};
};

// ---------------------------------------------------------------------------
// Device Results
// ---------------------------------------------------------------------------

/**
 * @struct DeviceInfo
 * @brief Audio output device information.
 * @ingroup caudio_cli
 *
 * @json_example {"id": "hw:0,0", "name": "Built-in Audio", "isDefault": true}
 *
 * @param id Device identifier.
 * @param name Human-readable device name.
 * @param isDefault Whether this is the default device.
 */
struct DeviceInfo final {
    /**
     * @brief Device identifier.
     */
    std::string id{};
    /**
     * @brief Human-readable device name.
     */
    std::string name{};
    /**
     * @brief Whether this is the default device.
     */
    bool isDefault{false};
};

/**
 * @struct Devices
 * @brief List of audio output devices (returned by DeviceList).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Devices", "devices": [{"id": "hw:0,0", "name": "Built-in Audio", "isDefault": true}, ...]}
 *
 * @param devices Vector of device information.
 */
struct Devices final {
    /**
     * @brief Vector of device information.
     */
    std::vector<DeviceInfo> devices{};
};

// ---------------------------------------------------------------------------
// Special Result Types
// ---------------------------------------------------------------------------

/**
 * @brief Empty result (success with no data) - uses std::monostate.
 * @ingroup caudio_cli
 */
using Empty = std::monostate;

/**
 * @brief Error type alias for CLI errors.
 * @ingroup caudio_cli
 *
 * @see caudio::utils::Error
 */
using CliError = caudio::utils::Error;

/**
 * @brief Variant type representing any valid CLI result or error.
 *
 * Used for JSON serialization/deserialization. Contains all possible
 * result types plus Empty for successful operations with no data,
 * and CliError for error responses.
 * @ingroup caudio_cli
 *
 * @see toJson for serialization
 * @see resultFromJson for deserialization
 * @see protocol.cppm for IPC framing
 */
using Result = std::variant<Status, QueueTracks, VolumeInfo, LibraryStatsData, LibraryStatsDetailedData, Tracks, Playlists,
                            PlaylistData, ConfigValue, ConfigValues, SingleTrack, TrackInfo, History, Empty, CliError,
                            Devices>;

/**
 * @brief Expected type for results that can fail with a CLI error.
 * @ingroup caudio_cli
 *
 * Used for functions that return either a Result or a CliError.
 * @see caudio::utils::Error
 */
using ReplyExpected = std::expected<Result, CliError>;

} // namespace caudio::cli
