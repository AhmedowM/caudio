#pragma once

/**
 * @file database.hpp
 * @brief Neutral persistence contract used by the playback engine.
 * @ingroup caudio_contracts
 */

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
};

} // namespace caudio::contracts
