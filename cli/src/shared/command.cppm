/**
 * @file command.cppm
 * @brief CLI command types and variants for the caudio IPC protocol.
 * @ingroup caudio_cli
 *
 * Defines all command structs that can be sent from a client to the
 * caudio service daemon. Commands are serialized as JSON over the
 * IPC socket/named pipe connection.
 *
 * ## IPC Protocol Structure
 *
 * Commands are sent as JSON objects with a "type" field identifying the command.
 * The full IPC message format is:
 * - 4-byte big-endian length prefix (uint32_t)
 * - JSON payload containing an IpcRequest with:
 *   - `id`: uint32_t request identifier
 *   - `cmd`: Command object with `type` and command-specific fields
 *
 * @see caudio::cli::protocol for serialization/deserialization functions
 * @see caudio::cli::Result for response types
 */

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

// ---------------------------------------------------------------------------
// Playback Control Commands
// ---------------------------------------------------------------------------

/**
 * @struct Play
 * @brief Start playback of the active queue.
 *
 * If no track is currently loaded, starts from the beginning of the queue.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Play"}
 */
struct Play final {};

/**
 * @struct Pause
 * @brief Pause the currently playing track.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Pause"}
 */
struct Pause final {};

/**
 * @struct Resume
 * @brief Resume playback from a paused state.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Resume"}
 */
struct Resume final {};

/**
 * @struct Restart
 * @brief Restart the current track from the beginning.
 *
 * Seeks to position 0 and ensures playback is started.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Restart"}
 */
struct Restart final {};

/**
 * @struct Stop
 * @brief Stop playback and clear the current track.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Stop"}
 */
struct Stop final {};

/**
 * @struct Next
 * @brief Advance to the next track in the queue.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Next"}
 */
struct Next final {};

/**
 * @struct Prev
 * @brief Go to the previous track in the queue.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Prev"}
 */
struct Prev final {};

/**
 * @struct Seek
 * @brief Seek to a specific position in the current track.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Seek", "seconds": 120.5}
 *
 * @param seconds Target position in seconds.
 */

struct Seek final {
    double seconds{0.0};
};

/**
 * @struct StatusReq
 * @brief Request the current playback status.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "StatusReq"}
 *
 * @return Status object with current playback state.
 */
struct StatusReq final {};

// ---------------------------------------------------------------------------
// Volume Commands
// ---------------------------------------------------------------------------

/**
 * @struct VolumeSet
 * @brief Set volume level, mute state, or apply a relative delta.
 *
 * Any combination of level, mute, and deltaPct can be provided.
 * If multiple are given, they are applied in order: level, then deltaPct, then mute.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "VolumeSet", "level": 50.0, "mute": false, "deltaPct": 10}
 *
 * @param level Absolute volume level as a percentage (0-100). If omitted, current level is preserved unless modified by deltaPct.
 * @param mute Mute state. If true, volume is set to 0. If false and volume was 0, restores to 50% (or level if provided).
 * @param deltaPct Relative volume change in percentage points (-100 to +100). Applied on top of current or specified level.
 */
struct VolumeSet final {
    /**
     * @brief Absolute volume level as a percentage (0-100).
     * If omitted, current level is preserved unless modified by deltaPct.
     */
    std::optional<float> level{};

    /**
     * @brief Mute state. If true, volume is set to 0.
     * If false and volume was 0, restores to 50% (or level if provided).
     */
    std::optional<bool> mute{};

    /**
     * @brief Relative volume change in percentage points (-100 to +100).
     * Applied on top of current or specified level.
     */
    std::optional<int> deltaPct{};
};

// ---------------------------------------------------------------------------
// Queue Commands
// ---------------------------------------------------------------------------

/**
 * @struct QueueList
 * @brief List all tracks in the active queue.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueList"}
 *
 * @return QueueTracks object with vector of tracks.
 */
struct QueueList final {};

/**
 * @struct QueueQueues
 * @brief List all available queues with their track counts.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueQueues"}
 *
 * @return LibraryStatsData object with queue/track/playlist counts.
 */
struct QueueQueues final {};

/**
 * @struct QueueSwitch
 * @brief Switch the active queue by ID.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueSwitch", "qid": 2}
 *
 * @param qid Queue ID to switch to (1-based).
 * @return Status object with updated playback state.
 */
struct QueueSwitch final {
    /**
     * @brief Queue ID to switch to (1-based).
     */
    int64_t qid{1};
};

/**
 * @struct QueueAdd
 * @brief Add track(s) to the active queue.
 *
 * Can add a single file by path, search by query, or add by track ID.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueAdd", "query": "song.mp3", "search": false}
 *
 * @param query Search query, file path, or track ID string.
 * @param search If true, treat query as a search term; otherwise as a path or ID.
 * @return QueueTracks object with updated queue contents.
 */
struct QueueAdd final {
    /**
     * @brief Search query, file path, or track ID string.
     */
    std::string query{};
    /**
     * @brief If true, treat query as a search term; otherwise as a path or ID.
     */
    bool search{false};
};

/**
 * @struct QueueRemove
 * @brief Remove a track from the active queue by position or track ID.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueRemove", "idOrIndex": "5"}
 *
 * @param idOrIndex Queue position (0-based) or track ID.
 * @return QueueTracks object with updated queue contents.
 */
struct QueueRemove final {
    /**
     * @brief Queue position (0-based) or track ID.
     */
    std::string idOrIndex{};
};

/**
 * @struct QueueMove
 * @brief Move a track within the queue from one position to another.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueMove", "from": 3, "to": 0}
 *
 * @param from Source position (0-based).
 * @param to Destination position (0-based).
 * @return QueueTracks object with updated queue contents.
 */
struct QueueMove final {
    /**
     * @brief Source position (0-based).
     */
    std::size_t from{0};
    /**
     * @brief Destination position (0-based).
     */
    std::size_t to{0};
};

/**
 * @struct QueueClear
 * @brief Remove all tracks from the active queue.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueClear"}
 *
 * @return QueueTracks object with empty queue.
 */
struct QueueClear final {};

/**
 * @struct QueueShuffle
 * @brief Enable, disable, or toggle shuffle mode for the active queue.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueShuffle", "on": true}
 *
 * @param on If set, explicitly enable (true) or disable (false). If omitted, toggles the current state.
 * @return Status object with updated shuffle state.
 */
struct QueueShuffle final {
    /**
     * @brief If set, explicitly enable (true) or disable (false).
     * If omitted, toggles the current state.
     */
    std::optional<bool> on{};
};

/**
 * @struct QueueRepeat
 * @brief Set or query repeat mode for the active queue.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "QueueRepeat", "mode": "One"}
 *
 * @param mode Repeat mode to set. If omitted, returns current mode via status.
 * @return Status object with updated repeat mode.
 */
struct QueueRepeat final {
    /**
     * @brief Repeat mode to set. If omitted, returns current mode via status.
     */
    std::optional<caudio::engine::RepeatMode> mode{};
};

// ---------------------------------------------------------------------------
// Playlist Commands
// ---------------------------------------------------------------------------

/**
 * @struct PlaylistList
 * @brief List all playlists.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistList"}
 *
 * @return Playlists object with vector of playlists.
 */
struct PlaylistList final {};

/**
 * @struct PlaylistTracks
 * @brief List tracks in a specific playlist.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistTracks", "pid": 1}
 *
 * @param pid Playlist ID.
 * @return PlaylistData object with tracks and format info.
 */
struct PlaylistTracks final {
    /**
     * @brief Playlist ID.
     */
    int64_t pid{0};
};

/**
 * @struct PlaylistLoad
 * @brief Load a playlist into the active queue and optionally start playback.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistLoad", "pid": 1, "play": true}
 *
 * @param pid Playlist ID to load.
 * @param play If true, start playback immediately after loading.
 * @return Status object with updated playback state.
 */
struct PlaylistLoad final {
    /**
     * @brief Playlist ID to load.
     */
    int64_t pid{0};
    /**
     * @brief If true, start playback immediately after loading.
     */
    bool play{false};
};

/**
 * @struct PlaylistSave
 * @brief Save the active queue as a new playlist.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistSave", "name": "My Playlist", "queue_id": 1}
 *
 * @param name Name for the new playlist.
 * @param queue_id Queue ID to save. If omitted, saves the active queue.
 * @return Empty (success with no data).
 */
struct PlaylistSave final {
    /**
     * @brief Name for the new playlist.
     */
    std::string name{};
    /**
     * @brief Queue ID to save. If omitted, saves the active queue.
     */
    std::optional<int64_t> queue_id{};
};

/**
 * @struct PlaylistDelete
 * @brief Delete a playlist by ID.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistDelete", "pid": 1}
 *
 * @param pid Playlist ID to delete.
 * @return Empty (success with no data).
 */
struct PlaylistDelete final {
    /**
     * @brief Playlist ID to delete.
     */
    int64_t pid{0};
};

/**
 * @struct PlaylistRename
 * @brief Rename an existing playlist.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistRename", "pid": 1, "newName": "New Name"}
 *
 * @param pid Playlist ID to rename.
 * @param newName New name for the playlist.
 * @return Empty (success with no data).
 */
struct PlaylistRename final {
    /**
     * @brief Playlist ID to rename.
     */
    int64_t pid{0};
    /**
     * @brief New name for the playlist.
     */
    std::string newName{};
};

/**
 * @struct PlaylistExport
 * @brief Export a playlist to a file (M3U format).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistExport", "pid": 1, "path": "/tmp/playlist.m3u", "format": "m3u"}
 *
 * @param pid Playlist ID to export.
 * @param path Output file path.
 * @param format Export format (currently only "m3u" supported).
 * @return Empty (success with no data).
 */
struct PlaylistExport final {
    /**
     * @brief Playlist ID to export.
     */
    int64_t pid{0};
    /**
     * @brief Output file path.
     */
    std::string path{};
    /**
     * @brief Export format (currently only "m3u" supported).
     */
    std::string format{"m3u"};
};

/**
 * @struct PlaylistImport
 * @brief Import a playlist from an M3U file.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "PlaylistImport", "path": "/tmp/playlist.m3u", "name": "Imported"}
 *
 * @param path Path to the M3U file.
 * @param name Optional name for the imported playlist. If omitted, derived from the filename.
 * @return Empty (success with no data).
 */
struct PlaylistImport final {
    /**
     * @brief Path to the M3U file.
     */
    std::string path{};
    /**
     * @brief Optional name for the imported playlist.
     * If omitted, derived from the filename.
     */
    std::optional<std::string> name{};
};

// ---------------------------------------------------------------------------
// Library Commands
// ---------------------------------------------------------------------------

/**
 * @struct LibraryScan
 * @brief Scan a directory for audio files and add them to the library.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibraryScan", "path": "/music", "mode": "sampled"}
 *
 * @param path Root directory to scan. If omitted, uses the music directory adjacent to the database.
 * @param mode Scan mode: "sampled" (fast, first/last 64KB) or "full" (entire file). Defaults to "sampled".
 * @return Empty (success with no data).
 */
struct LibraryScan final {
    /**
     * @brief Root directory to scan. If omitted, uses the music directory
     * adjacent to the database.
     */
    std::optional<std::string> path{};
    /**
     * @brief Scan mode: "sampled" (fast, first/last 64KB) or "full" (entire file).
     * Defaults to "sampled".
     */
    std::string mode{"sampled"};
};

/**
 * @struct LibrarySearch
 * @brief Search the library using full-text search.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibrarySearch", "query": "beatles", "limit": 50}
 *
 * @param query Search query string.
 * @param limit Maximum number of results to return.
 * @return Tracks object with vector of matching tracks.
 */
struct LibrarySearch final {
    /**
     * @brief Search query string.
     */
    std::string query{};
    /**
     * @brief Maximum number of results to return.
     */
    int limit{50};
};

/**
 * @struct LibraryStats
 * @brief Get basic library statistics (track/queue/playlist counts).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibraryStats"}
 *
 * @return LibraryStatsData object with counts.
 */
struct LibraryStats final {};

/**
 * @struct LibraryStatsDetailed
 * @brief Get detailed library statistics including play history.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibraryStatsDetailed"}
 *
 * @return LibraryStatsDetailedData object with detailed statistics and most-played tracks.
 */
struct LibraryStatsDetailed final {};

/**
 * @struct LibraryAdd
 * @brief Add a file or directory to the library with metadata extraction.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibraryAdd", "path": "/music/new.mp3", "recursive": false}
 *
 * @param path Path to audio file or directory.
 * @param recursive If true and path is a directory, scan recursively.
 * @return Empty (success with no data).
 */
struct LibraryAdd final {
    /**
     * @brief Path to audio file or directory.
     */
    std::string path{};
    /**
     * @brief If true and path is a directory, scan recursively.
     */
    bool recursive{false};
};

/**
 * @struct LibraryRemove
 * @brief Remove a track from the library by ID or path.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibraryRemove", "query": "123"}
 *
 * @param query Track ID or path to remove.
 * @return Empty (success with no data).
 */
struct LibraryRemove final {
    /**
     * @brief Track ID or path to remove.
     */
    std::string query{};
};

/**
 * @struct LibraryList
 * @brief List library tracks with optional filtering and pagination.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "LibraryList", "query": "rock", "limit": 20, "offset": 0, "artist": "Beatles", "album": "Abbey Road", "genre": "Rock"}
 *
 * @param query Optional search query.
 * @param limit Maximum results to return.
 * @param offset Offset for pagination.
 * @param artist Filter by artist.
 * @param album Filter by album.
 * @param genre Filter by genre.
 * @return Tracks object with vector of matching tracks.
 */
struct LibraryList final {
    /**
     * @brief Optional search query.
     */
    std::optional<std::string> query{};
    /**
     * @brief Maximum results to return.
     */
    int limit{50};
    /**
     * @brief Offset for pagination.
     */
    int offset{0};
    /**
     * @brief Filter by artist.
     */
    std::optional<std::string> artist{};
    /**
     * @brief Filter by album.
     */
    std::optional<std::string> album{};
    /**
     * @brief Filter by genre.
     */
    std::optional<std::string> genre{};
};

// ---------------------------------------------------------------------------
// Tag Commands
// ---------------------------------------------------------------------------

/**
 * @struct TagEdit
 * @brief Edit a metadata tag for a track.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "TagEdit", "id": 123, "field": "artist", "value": "New Artist"}
 *
 * @param id Track ID.
 * @param field Tag field name (e.g., "title", "artist", "album", "genre", "year", "track_number", "disc_number", "album_artist").
 * @param value New value for the tag.
 * @return Empty (success with no data).
 */
struct TagEdit final {
    /**
     * @brief Track ID.
     */
    int64_t id{0};
    /**
     * @brief Tag field name (e.g., "title", "artist", "album", "genre", "year",
     * "track_number", "disc_number", "album_artist").
     */
    std::string field{};
    /**
     * @brief New value for the tag.
     */
    std::string value{};
};

/**
 * @struct TagGet
 * @brief Get metadata for a track.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "TagGet", "id": 123}
 *
 * @param id Track ID.
 * @return SingleTrack object with full track metadata.
 */
struct TagGet final {
    /**
     * @brief Track ID.
     */
    int64_t id{0};
};

// ---------------------------------------------------------------------------
// Config Commands
// ---------------------------------------------------------------------------

/**
 * @struct ConfigGet
 * @brief Get a configuration value by key.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "ConfigGet", "key": "volume"}
 *
 * @param key Configuration key.
 * @return ConfigValue object with key and value.
 */
struct ConfigGet final {
    /**
     * @brief Configuration key.
     */
    std::string key{};
};

/**
 * @struct ConfigSet
 * @brief Set a configuration value.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "ConfigSet", "key": "volume", "value": "0.8"}
 *
 * @param key Configuration key.
 * @param value Configuration value (JSON-encoded if complex type).
 * @return Empty (success with no data).
 */
struct ConfigSet final {
    /**
     * @brief Configuration key.
     */
    std::string key{};
    /**
     * @brief Configuration value (JSON-encoded if complex type).
     */
    std::string value{};
};

/**
 * @struct ConfigList
 * @brief List all configuration values.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "ConfigList"}
 *
 * @return ConfigValues object with vector of key-value pairs.
 */
struct ConfigList final {};

/**
 * @struct ConfigExport
 * @brief Export configuration to a file.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "ConfigExport", "path": "/tmp/config.json"}
 *
 * @param path Output file path.
 * @return Empty (success with no data).
 */
struct ConfigExport final {
    /**
     * @brief Output file path.
     */
    std::string path{};
};

/**
 * @struct ConfigImport
 * @brief Import configuration from a file.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "ConfigImport", "path": "/tmp/config.json"}
 *
 * @param path Input file path.
 * @return Empty (success with no data).
 */
struct ConfigImport final {
    /**
     * @brief Input file path.
     */
    std::string path{};
};

/**
 * @struct ConfigReset
 * @brief Reset a specific config key or all configuration to defaults.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "ConfigReset", "key": "volume"}
 *
 * @param key Key to reset. If omitted, resets all configuration.
 * @return Empty (success with no data).
 */
struct ConfigReset final {
    /**
     * @brief Key to reset. If omitted, resets all configuration.
     */
    std::optional<std::string> key{};
};

// ---------------------------------------------------------------------------
// History Commands
// ---------------------------------------------------------------------------

/**
 * @struct HistoryList
 * @brief List playback history entries.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "HistoryList", "limit": 50}
 *
 * @param limit Maximum number of entries to return. If omitted, returns 50.
 * @return History object with vector of history entries.
 */
struct HistoryList final {
    /**
     * @brief Maximum number of entries to return. If omitted, returns 50.
     */
    std::optional<int> limit{};
};

/**
 * @struct HistoryClear
 * @brief Clear all playback history.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "HistoryClear"}
 *
 * @return Empty (success with no data).
 */
struct HistoryClear final {};

// ---------------------------------------------------------------------------
// Info and Utility Commands
// ---------------------------------------------------------------------------

/**
 * @struct Info
 * @brief Get detailed information about the currently playing track.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Info"}
 *
 * @return TrackInfo object with track metadata and play statistics.
 */
struct Info final {};

/**
 * @struct Shutdown
 * @brief Request the service daemon to shut down.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Shutdown"}
 *
 * @return Empty (success with no data).
 */
struct Shutdown final {};

/**
 * @struct Preview
 * @brief Preview an audio file (play without adding to queue).
 * @ingroup caudio_cli
 *
 * @json_example {"type": "Preview", "file": "/music/song.mp3"}
 *
 * @param file Path to audio file.
 * @return Empty (success with no data).
 */
struct Preview final {
    /**
     * @brief Path to audio file.
     */
    std::string file{};
};

// ---------------------------------------------------------------------------
// Device Commands
// ---------------------------------------------------------------------------

/**
 * @struct DeviceList
 * @brief List available audio output devices.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "DeviceList"}
 *
 * @return Devices object with vector of device information.
 */
struct DeviceList final {};

/**
 * @struct DeviceSet
 * @brief Set the active audio output device.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "DeviceSet", "id": "hw:0,0"}
 *
 * @param id Device ID to set as active.
 * @return Empty (success with no data).
 */
struct DeviceSet final {
    /**
     * @brief Device ID to set as active.
     */
    std::string id{};
};

/**
 * @struct DeviceTest
 * @brief Test an audio output device with a test tone.
 * @ingroup caudio_cli
 *
 * @json_example {"type": "DeviceTest", "id": "hw:0,0"}
 *
 * @param id Device ID to test. If omitted, tests the default device.
 * @return Empty (success with no data).
 */
struct DeviceTest final {
    /**
     * @brief Device ID to test. If omitted, tests the default device.
     */
    std::optional<std::string> id{};
};

/**
 * @brief Variant type representing any valid CLI command.
 *
 * Used for JSON serialization/deserialization and type-safe dispatch.
 * All command structs are empty or contain only data members (no behavior).
 * @ingroup caudio_cli
 *
 * @see toJson for serialization
 * @see commandFromJson for deserialization
 * @see protocol.cppm for IPC framing
 */
using Command =
    std::variant<Play, Pause, Resume, Restart, Stop, Next, Prev, Seek, StatusReq, VolumeSet,
                 QueueList, QueueQueues, QueueSwitch, QueueAdd, QueueRemove, QueueMove, QueueClear,
                 QueueShuffle, QueueRepeat, PlaylistList, PlaylistTracks, PlaylistLoad,
                 PlaylistSave, PlaylistDelete, PlaylistRename, PlaylistExport, PlaylistImport,
                 LibraryScan, LibrarySearch, LibraryStats, LibraryStatsDetailed, LibraryAdd, LibraryRemove, LibraryList, TagEdit,
                 TagGet, ConfigGet, ConfigSet, ConfigList, ConfigExport, ConfigImport, ConfigReset,
                 HistoryList, HistoryClear, Shutdown, Preview, DeviceList, DeviceSet, DeviceTest, Info>;

// helper concepts
/**
 * @brief Concept matching any valid command alternative type.
 *
 * Constrains template parameters to types that are part of the Command variant.
 * @ingroup caudio_cli
 */
template <typename T>
concept CommandAlternative = requires {
    requires std::disjunction_v<
        std::is_same<T, Play>, std::is_same<T, Pause>, std::is_same<T, Resume>,
        std::is_same<T, Restart>, std::is_same<T, Stop>, std::is_same<T, Next>,
        std::is_same<T, Prev>, std::is_same<T, Seek>, std::is_same<T, StatusReq>,
        std::is_same<T, VolumeSet>, std::is_same<T, QueueList>, std::is_same<T, QueueQueues>,
        std::is_same<T, QueueSwitch>, std::is_same<T, QueueAdd>, std::is_same<T, QueueRemove>,
        std::is_same<T, QueueMove>, std::is_same<T, QueueClear>, std::is_same<T, QueueShuffle>,
        std::is_same<T, QueueRepeat>, std::is_same<T, PlaylistList>,
        std::is_same<T, PlaylistTracks>, std::is_same<T, PlaylistLoad>,
        std::is_same<T, PlaylistSave>, std::is_same<T, PlaylistDelete>,
        std::is_same<T, PlaylistRename>, std::is_same<T, PlaylistExport>, std::is_same<T, PlaylistImport>,
        std::is_same<T, LibraryScan>, std::is_same<T, LibrarySearch>, std::is_same<T, LibraryStats>,
        std::is_same<T, LibraryStatsDetailed>, std::is_same<T, LibraryAdd>, std::is_same<T, LibraryRemove>,
        std::is_same<T, LibraryList>, std::is_same<T, TagEdit>, std::is_same<T, TagGet>,
        std::is_same<T, ConfigGet>, std::is_same<T, ConfigSet>,
        std::is_same<T, ConfigList>, std::is_same<T, ConfigExport>, std::is_same<T, ConfigImport>,
        std::is_same<T, ConfigReset>, std::is_same<T, HistoryList>, std::is_same<T, HistoryClear>,
        std::is_same<T, Shutdown>, std::is_same<T, Preview>, std::is_same<T, DeviceList>,
        std::is_same<T, DeviceSet>, std::is_same<T, DeviceTest>, std::is_same<T, Info>>;
};

/**
 * @brief Concept matching any valid command type (including cvref-qualified).
 *
 * Convenience concept that strips cvref qualifiers before checking CommandAlternative.
 * @ingroup caudio_cli
 */
template <typename T>
concept CommandType = CommandAlternative<std::remove_cvref_t<T>>;

} // namespace caudio::cli
