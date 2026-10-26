#pragma once

/**
 * @file database.hpp
 * @brief Neutral persistence contract used by the playback engine.
 * @ingroup caudio_contracts
 */

#include <caudio/fwd.hpp>
#include <caudio/types/record.hpp>
#include <caudio/types/wire.hpp>
#include <caudio/utils/error.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace caudio::contracts {

struct PlaybackState {
    int shuffle_enabled{};
    caudio::types::RepeatMode repeat_mode{caudio::types::RepeatMode::Off};
    int64_t cursor_pos{};
    int64_t current_track_id{};
    float volume{1.0f};
    int64_t active_queue_id{1};
    float pre_mute_volume{0.5f};
    int64_t position_ms{};
    std::vector<int64_t> shuffle_perm;
};

struct PlaybackStats {
    int64_t num_tracks{};
    int64_t num_playlists{};
    int64_t num_queue_items{};
    int64_t num_history{};
    int64_t num_bookmarks{};
    int64_t num_libraries{};
    int64_t total_duration_ms{};
};

class IDatabase {
  public:
    virtual ~IDatabase() = default;

    virtual bool isOpen() const noexcept = 0;
    virtual std::optional<std::size_t> queueIndexOf(int64_t qid, int64_t trackId) = 0;
    virtual std::size_t queueCount(int64_t qid) = 0;
    virtual std::expected<PlaybackStats, caudio::utils::Error> playbackStats() = 0;
    virtual std::expected<bool, caudio::utils::Error> queueExists(int64_t qid) = 0;
    virtual std::expected<caudio::types::Track, caudio::utils::Error> getTrack(int64_t id) = 0;
    virtual std::expected<caudio::types::Track, caudio::utils::Error>
    queueTrackAt(int64_t qid, int64_t position) = 0;

    virtual std::expected<PlaybackState, caudio::utils::Error> loadEngineState() = 0;
    virtual std::expected<void, caudio::utils::Error>
    saveEngineState(const PlaybackState& state) = 0;
    virtual std::expected<void, caudio::utils::Error>
    saveEngineShuffle(bool enabled, std::span<const int64_t> permutation,
                      int64_t cursor_position) = 0;
    virtual std::expected<void, caudio::utils::Error> saveEngineCursor(int64_t cursor_position) = 0;
    virtual std::expected<void, caudio::utils::Error>
    markPlayed(int64_t track_id, int64_t started_at, int64_t completed_at, int64_t position_ms,
               double completion_pct, int64_t queue_id) = 0;
    virtual std::expected<std::vector<caudio::types::HistoryEntry>, caudio::utils::Error>
    engineHistoryList(int limit) = 0;
    virtual std::expected<void, caudio::utils::Error> engineHistoryClear() = 0;
    virtual std::expected<int64_t, caudio::utils::Error>
    insertTrack(const caudio::types::Track& t) = 0;
    virtual std::expected<caudio::types::Track, caudio::utils::Error>
    findByFingerprint(const std::array<uint8_t, 32>& fp) = 0;
    virtual std::expected<caudio::types::Track, caudio::utils::Error>
    findByPath(std::string_view path) = 0;
    virtual std::expected<caudio::db::Queue, caudio::utils::Error> getQueue(int64_t qid) = 0;
    virtual std::expected<std::vector<caudio::types::QueueItem>, caudio::utils::Error>
    listQueueItems(int64_t qid) = 0;
    virtual std::expected<std::vector<caudio::db::Queue>, caudio::utils::Error> listQueues() = 0;
    virtual std::expected<int64_t, caudio::utils::Error> createQueue(std::string_view name,
                                                                     int64_t library_id = 1) = 0;
    virtual std::expected<void, caudio::utils::Error> deleteQueue(int64_t qid) = 0;
    virtual std::expected<void, caudio::utils::Error> setQueueTemp(int64_t qid, bool temp) = 0;
    virtual std::expected<void, caudio::utils::Error>
    queueEnqueueBatch(int64_t qid, std::span<const int64_t> tids) = 0;
    virtual std::expected<void, caudio::utils::Error>
    queueEnqueueBatch(int64_t qid, const std::vector<int64_t>& tids) = 0;
    virtual std::expected<void, caudio::utils::Error> queueEnqueue(int64_t qid, int64_t tid,
                                                                   int64_t pos = -1) = 0;
    virtual std::expected<void, caudio::utils::Error> queueRemove(int64_t qid, int64_t pos) = 0;
    virtual std::expected<void, caudio::utils::Error>
    queueReplaceAll(int64_t qid, std::span<const int64_t> ids) = 0;
    virtual std::expected<void, caudio::utils::Error>
    queueReplaceAll(int64_t qid, const std::vector<int64_t>& ids) = 0;
    virtual std::expected<void, caudio::utils::Error> queueClear(int64_t qid) = 0;
    virtual std::expected<void, caudio::utils::Error>
    updateTrack(const caudio::types::Track& t) = 0;
    virtual std::expected<std::vector<db::Library>, caudio::utils::Error> listLibraries() = 0;
    virtual std::expected<db::DbStats, caudio::utils::Error> getStats() = 0;
    virtual std::expected<caudio::types::LibraryStats, caudio::utils::Error>
    libraryStats(std::optional<int> mostPlayed = std::nullopt) = 0;
    virtual std::expected<std::vector<caudio::types::Track>, caudio::utils::Error>
    listTracks(const db::TrackQuery* q = nullptr) = 0;
    virtual std::expected<std::vector<caudio::types::Bookmark>, caudio::utils::Error>
    listBookmarks(int64_t tid) = 0;
    virtual std::expected<void, caudio::utils::Error> bookmarkAdd(int64_t tid, int64_t pos,
                                                                  std::string_view note = {}) = 0;
    virtual std::expected<void, caudio::utils::Error> bookmarkRemove(int64_t id) = 0;
    virtual std::expected<caudio::types::Bookmark, caudio::utils::Error>
    bookmarkGet(int64_t id) = 0;
    virtual std::expected<void, caudio::utils::Error> deleteTrack(int64_t id) = 0;
    virtual std::expected<std::vector<caudio::types::Playlist>, caudio::utils::Error>
    listPlaylists() = 0;
    virtual std::expected<std::vector<caudio::types::Track>, caudio::utils::Error>
    playlistGetTracks(int64_t pid) = 0;
    virtual std::expected<caudio::types::Playlist, caudio::utils::Error>
    getPlaylist(int64_t id) = 0;
    virtual std::expected<int64_t, caudio::utils::Error>
    createPlaylist(std::string_view name, int type = 0, std::string_view smart_query = {}) = 0;
    virtual std::expected<void, caudio::utils::Error> playlistAddTrack(int64_t pid, int64_t tid,
                                                                       int64_t pos = -1) = 0;
    virtual std::expected<void, caudio::utils::Error> playlistRemoveTrack(int64_t pid,
                                                                          int64_t tid) = 0;
    virtual std::expected<void, caudio::utils::Error> playlistReorder(int64_t pid, int64_t from,
                                                                      int64_t to) = 0;
    virtual std::expected<void, caudio::utils::Error> renamePlaylist(int64_t id,
                                                                     std::string_view newName) = 0;
    virtual std::expected<int64_t, caudio::utils::Error>
    createPlaylistFromTracks(std::string_view name, std::span<const int64_t> trackIds) = 0;
    virtual std::expected<void, caudio::utils::Error> deletePlaylist(int64_t id) = 0;
    virtual caudio::utils::Logger* logger() const noexcept = 0;
    virtual std::expected<void, caudio::utils::Error> libraryUpdate(const db::Library& l) = 0;
    virtual std::expected<void, caudio::utils::Error> libraryDelete(int64_t id) = 0;
};

} // namespace caudio::contracts
