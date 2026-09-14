module;
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file engine_types.cppm
 * @brief Core value types for the caudio playback engine.
 * @ingroup caudio_engine
 * @defgroup caudio_engine caudio engine
 * @brief Playback engine — state machine, shuffle/history, gapless and persistence.
 *
 * @details The caudio_engine module group aggregates the playback state
 * machine, queue/shuffle permutation, gapless transition, history
 * thresholds, monitor/decode loops and SQLite-backed persistence.
 * All public symbols are exported under `caudio::engine` via
 * `caudio.engine:types`, `caudio.engine:history`, `caudio.engine:shuffle`
 * and `caudio.engine` (aggregate).
 *
 * Thread safety: Engine synchronizes queue state via `queueMutex_`
 * (try_lock pattern for public mutators), decode/ring access via
 * `decodeMtx_` + `decodeCv_`, and DB access via `Database::mutex()`
 * (shared_mutex). Persistence uses `engine_state` (see Engine::loadState
 * / saveState) with fallback for missing `active_queue_id`.
 *
 * State machine: `Stopped -> Playing -> Paused -> Playing`, with
 * `next()`/`prev()` advancing `QueueState::cursor` subject to
 * `RepeatMode` and `QueueState::shuffle`/`perm` (see
 * Engine::queueNextLocked / queuePrevLocked).
 *
 * Gapless: `Engine::engineTick` arms `gaplessArmed_` when
 * `remaining <= gaplessMs` (default 300 ms) and calls `next()` as
 * single-writer CAS; `decodeLoop` fills the `SpscRing<float>` and
 * `preroll()` pre-fills half the ring before `AudioOutput::start()`.
 *
 * Shuffle: `QueueState::perm` holds a permutation of `[0, queueCount)`;
 * `detail::shufflePerm` shuffles via `std::mt19937` seeded by
 * `random_device` (production) or caller-supplied `mt19937` (seed 42
 * for deterministic tests); persisted as BLOB `shuffle_perm`.
 *
 * History: `EngineConfig::historyThresholdPct` (default 60) and
 * `historyThresholdSecs` (default 90) feed
 * `detail::shouldMarkPlayedEx()`; `doHistoryMark()` does a single
 * `compare_exchange_strong` on `markedPlayed_` and a
 * `BEGIN IMMEDIATE` transaction that bumps `tracks.play_count` and
 * inserts into `history`.
 */

export module caudio.engine:types;

import caudio.utils;

export namespace caudio::engine {

/**
 * @brief Repeat mode for queue advancement.
 * @ingroup caudio_engine
 * @details Controls wrap behaviour in Engine::queueNextLocked and
 * Engine::queuePrevLocked when `cursor` reaches the end or start.
 * - `Off`: linear; shuffle wrappers reshuffle and non-shuffle wraps to 0.
 * - `Queue`: same wrap as Off; single-track repeat is only via `One`.
 * - `One`: repeats the current track (seek 0 / stay on last position)
 *   without advancing the cursor.
 * @see QueueState
 * @see Engine::setRepeat
 */
enum class RepeatMode : int { Off = 0, Queue = 1, One = 2 };

/**
 * @brief Deprecated shuffle toggle — use QueueState::shuffle.
 * @ingroup caudio_engine
 * @deprecated Unused; kept for ABI compat. Use QueueState::shuffle
 * and Engine::setShuffle(). Will be removed in the next major.
 */
enum class ShuffleMode : int { Off = 0, On = 1 };

/**
 * @brief Playback state machine.
 * @ingroup caudio_engine
 * @details State transitions (Engine):
 * - `Stopped -> Playing` via Engine::play() (queueNextLocked + doPlayTrack).
 * - `Playing -> Paused` via Engine::pause() (records pausePos_).
 * - `Paused -> Playing` via Engine::resume() / play() resume path.
 * - `Playing -> Playing` (restart) via play() when already playing (seek 0).
 * - Any -> `Stopped` via Engine::stop().
 * Backed by `std::atomic<PlaybackState>` (`playbackState_`).
 * @see Engine::state
 * @see Engine::play
 * @see Engine::pause
 */
enum class PlaybackState : int { Stopped = 0, Ready = 1, Playing = 2, Paused = 3 };

/**
 * @brief Engine event types emitted via MpscQueue.
 * @ingroup caudio_engine
 * @see EngineEvent
 * @see Engine::pollEvent
 */
enum class EngineEventType : int {
    None = 0,        ///< No event.
    TrackStarted = 1, ///< New track started (pushEvent from doPlayTrack / next).
    TrackEnded = 2,   ///< Reserved (emitted via callbacks with pct).
    QueueChanged = 3, ///< Queue/shuffle switched (setShuffle/switchQueue).
    Progress = 4,     ///< Periodic progress tick (~500 ms, see engineTick).
    Error = 5         ///< Internal error forwarded to on_error.
};

/**
 * @brief Event payload queued to Engine::eventQueue_.
 * @ingroup caudio_engine
 * @details Produced by Engine::pushEvent and consumed via
 * Engine::pollEvent / drainEvents / drainAll. Progress events carry
 * position/duration; TrackStarted carries track_id/queue_id/duration.
 */
struct EngineEvent {
    EngineEventType type{EngineEventType::None}; ///< Event kind.
    int64_t track_id{0};  ///< Affected track id (if any).
    int64_t queue_id{1};  ///< Active queue id at emission.
    double position{0.0}; ///< Position in seconds (Progress/TrackEnded).
    double duration{0.0}; ///< Duration in seconds.
    std::string msg{};    ///< Error text for Error events.
};

/**
 * @brief User callbacks invoked synchronously from pushEvent.
 * @ingroup caudio_engine
 * @details Callbacks are copied under `cbMutex_` then invoked outside
 * the event queue lock. Must be thread-safe if they touch shared state.
 * `on_track_ended` receives `(track_id, pct)` where pct is
 * `position/duration*100` clamped to [0,100].
 */
struct EngineCallbacks {
    std::function<void(int64_t track_id)> on_track_started{}; ///< Fired on TrackStarted.
    std::function<void(int64_t track_id, double pct)> on_track_ended{}; ///< Fired on TrackEnded with completion pct.
    std::function<void(int64_t queue_id)> on_queue_changed{}; ///< Fired on QueueChanged.
    std::function<void(caudio::utils::StatusCode err, std::string_view msg)> on_error{}; ///< Fired on Error.
    void* user{nullptr}; // unused — reserved
};

/**
 * @brief Configuration for Engine construction.
 * @ingroup caudio_engine
 * @details `pollMs` controls monitorLoop sleep (default 10 ms);
 * `gaplessMs` is the preroll window for gapless transition (default 300 ms);
 * `historyThresholdPct`/`historyThresholdSecs` are thresholds for
 * shouldMarkPlayedEx (defaults 60 / 90). `enableMonitorThread` gates
 * monitorLoop creation.
 */
struct EngineConfig {
    bool enableMonitorThread{true}; ///< Whether to start monitorLoop.
    int pollMs{10};                 ///< Monitor poll interval in ms (10 ms).
    int gaplessMs{300};             ///< Gapless preroll window in ms (300 ms).
    int historyThresholdPct{60};    ///< History pct threshold (60%).
    int historyThresholdSecs{90};   ///< History absolute seconds threshold (90 s).
    EngineCallbacks callbacks{};    ///< Initial callbacks (also via setCallbacks).
};

/**
 * @brief In-memory queue cursor and shuffle state.
 * @ingroup caudio_engine
 * @details `perm` is empty when shuffle is off; when on it holds a
 * permutation of `[0, queueCount)` shuffled via detail::shufflePerm.
 * `cursor` is the next index to consume (persisted as `cursor_pos`).
 * `queue_id` is the active queue (persisted as `active_queue_id`).
 * Protected by `Engine::queueMutex_` for writers; readers use atomics
 * or hold the mutex.
 */
struct QueueState {
    bool shuffle{false};              ///< Whether shuffle is enabled.
    RepeatMode repeat{RepeatMode::Off}; ///< Repeat mode.
    std::vector<int64_t> perm{};      ///< Shuffle permutation (positions, not track_ids).
    size_t cursor{0};                 ///< Next position index in perm or linear queue.
    int64_t queue_id{1};              ///< Active queue id.
};

/**
 * @brief Persisted engine state (row `engine_state.id=1`).
 * @ingroup caudio_engine
 * @details Mirrors columns `shuffle_enabled, repeat_mode, cursor_pos,
 * current_track_id, volume, shuffle_perm (BLOB), active_queue_id`.
 * `active_queue_id` may be missing on old DBs — loadState falls back
 * to `1` and saveState tries `sqlNew` then `sqlOld`. `shuffle_perm`
 * is `perm` serialized as `int64_t` blob.
 * @see Engine::loadState
 * @see Engine::saveState
 */
struct EngineState {
    int shuffleEnabled{0};               ///< Persisted shuffle flag (0/1).
    RepeatMode repeatMode{RepeatMode::Off}; ///< Persisted repeat mode.
    int64_t cursorPos{0};                ///< Persisted cursor (QueueState::cursor).
    int64_t currentTrackId{0};           ///< Last current track id.
    float volume{1.0f};                  ///< Persisted volume [0,1].
    int64_t activeQueueId{1};            ///< Persisted active queue id.
};

} // namespace caudio::engine
