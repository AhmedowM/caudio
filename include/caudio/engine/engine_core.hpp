#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/**
 * @file engine_core.hpp
 * @brief Playback engine -- state machine, gapless, decode/monitor loops and persistence.
 * @ingroup caudio_engine
 * @details Core `Engine` class (`caudio.engine` re-exports it via `engine.hpp`).
 * History/shuffle internals live in src/engine/ (NOT installed).
 * Core class is Engine which owns:
 * - Playback state machine (`Stopped -> Playing -> Paused -> Playing`,
 *   next/prev with shuffle/repeat, see queueNextLocked/queuePrevLocked).
 * - Gapless transition: preroll() fills SpscRing<float> to half capacity
 *   before AudioOutput::start(); decodeLoop continuously refills the ring;
 *   engineTick arms gaplessArmed_ (CAS 0->1) when remaining <= gaplessMs
 *   (300 ms) and calls next().
 * - Threads: decodeLoop runs on decodeThread_ (SPSC ring, decodeMtx_ +
 *   decodeCv_, wakes on Playing); monitorLoop runs on monitorThread_
 *   polling every pollMs (default 10 ms) via monMtx_/monCv_ and calling
 *   engineTick() (history + progress + gapless).
 * - Persistence: engine_state row id=1 stores shuffle_enabled,
 *   repeat_mode, cursor_pos, current_track_id, volume, shuffle_perm BLOB
 *   and active_queue_id. loadState/saveState handle the active_queue_id
 *   migration (try sqlNew, fallback to sqlOld). persistShuffleBlobLocked
 *   and persistCursorLocked update the blob/cursor transactionally.
 * - History: doHistoryMark uses shouldMarkPlayed(historyThresholdPct/Secs)
 *   with atomic CAS on markedPlayed_ and a BEGIN IMMEDIATE transaction.
 * Locking: queueMutex_ (mutex, try_lock) for QueueState; decodeMtx_
 * (mutex) for decoder_/ring_ vs seek/decodeLoop; queue/state DB writes
 * go through Database::mutex() (shared_mutex) + withTransaction.
 * Events: MpscQueue<EngineEvent,64> with drop-on-full, dispatched via
 * pushEvent to callbacks under cbMutex_.
 */

#include <caudio/db/db_types.hpp>
#include <caudio/engine/engine_types.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/mpsc_queue.hpp>
#include <caudio/utils/ring.hpp>

// Forward declarations for SQLite handles (sqlite3.h stays in .cpp files).
struct sqlite3;
struct sqlite3_stmt;

namespace caudio::db {
// Full type in db_core.hpp; Engine holds it by shared_ptr only.
class Database;
} // namespace caudio::db

namespace caudio::player {
// Player components held by unique_ptr (Engine ctor/dtor out-of-line).
class Reader;
class Decoder;
class AudioOutput;
} // namespace caudio::player

namespace caudio::engine {

/**
 * @brief Main playback engine -- queue, decoding, gapless, history and persistence.
 * @ingroup caudio_engine
 * @details See file-level docs for state machine, threading and persistence.
 * Public mutators that touch QueueState use `tryLockQueue()` (non-blocking);
 * callers get `StatusCode::Busy` if the queue is contended. Decode/ring
 * access is serialized by `decodeMtx_`. State (`playbackState_`,
 * `hasCurrent_`, `volume_`, `markedPlayed_`, `gaplessArmed_`,
 * `lastProgressMs_`) is atomic.
 *
 * Error codes: `InvalidArg` (null/empty args), `State` (no db / not
 * playing/paused), `NotFound` (empty queue / no event), `Busy` (queue
 * locked / transaction begin failed), `Internal` (SQLite / decoder),
 * `NoMem` (perm too large).
 *
 * Thread safety: thread-safe for concurrent play/pause/next/prev/seek
 * under the documented locks; `queueMutex_` is try_lock so callers
 * must handle Busy.
 * @see PlaybackState
 * @see QueueState
 * @see EngineConfig
 */
class Engine final {
  public:
    using ExpectedVoid = std::expected<void, caudio::utils::Error>; ///< Void or Error.
    using ExpectedEngine =
        std::expected<std::unique_ptr<Engine>, caudio::utils::Error>; ///< Engine ptr or Error.

    /**
     * @brief Creates an Engine with no database attached.
     * @ingroup caudio_engine
     * @param cfg Engine configuration (poll/gapless/history thresholds).
     * @return Owning Engine or Error from init().
     * @details Starts decodeThread_ and optionally monitorThread_ via init().
     * No DB is opened; attach via open() or attachDatabase().
     * @par Thread safety
     * Thread-safe; constructs internal atomics/threads.
     * @see open
     * @see attachDatabase
     */
    static ExpectedEngine create(const EngineConfig& cfg = {});

    /**
     * @brief Opens (or creates) a database and creates an Engine.
     * @ingroup caudio_engine
     * @param path Filesystem path (passed to Database::open).
     * @param cfg Engine configuration.
     * @return Owning Engine or Error (Io/Corrupt from Database::open).
     * @details Opens the DB, attaches it, loads persisted state via
     * loadState() (active_queue_id migration) and restores volume to
     * the AudioOutput if present.
     * @par Thread safety
     * Thread-safe; see Database::open.
     * @see Database::open
     * @see loadState
     */
    static ExpectedEngine open(std::string_view path, const EngineConfig& cfg = {});

    /**
     * @brief Attaches an already-open Database.
     * @ingroup caudio_engine
     * @param db Shared Database handle (must be non-null and open).
     * @return Success or Error InvalidArg / loadState failure.
     * @retval StatusCode::InvalidArg if db is null or handle is null.
     * @par Thread safety
     * Thread-safe; calls loadState() under dbMutex_.
     * @see loadState
     */
    ExpectedVoid attachDatabase(std::shared_ptr<caudio::db::Database> db);

    /** @brief Tears down threads and persists state. @ingroup caudio_engine */
    ~Engine();

    /**
     * @brief Shuts down monitor/decode threads and persists state.
     * @ingroup caudio_engine
     * @details Stops monRun_/decodeRun_, notifies cvs, joins jthreads
     * via request_stop(), persists cursorPos from queue_.cursor via
     * saveState(), then stops AudioOutput and resets decoder/reader/ring.
     * Idempotent.
     * @par Thread safety
     * Thread-safe; joins threads and locks where needed.
     */
    void shutdown();

    /**
     * @brief Starts or resumes playback.
     * @ingroup caudio_engine
     * @param queueId Active queue id (0 defaults to 1).
     * @return Success or Error State/Busy/NotFound/Internal.
     * @retval StatusCode::State if no db.
     * @retval StatusCode::Busy if queueMutex_ try_lock fails.
     * @retval StatusCode::NotFound if queue empty, or track file not
     * playable without allowSimulatedPlayback.
     * @retval StatusCode::Device if the audio device cannot be created
     * without allowSimulatedPlayback.
     * @details State machine:
     * - Paused -> Playing: resume (no dequeue), restart monotonic clock,
     *   start AudioOutput.
     * - Playing -> Playing: restart current track (seek 0 under decodeMtx_,
     *   ring reset).
     * - Stopped -> Playing: queueNextLocked under queueMutex_, then
     *   doPlayTrack.
     * Gapless: doPlayTrack prerolls and pushes TrackStarted event.
     * @par Thread safety
     * Thread-safe; uses tryLockQueue() and decodeMtx_ for decoder.
     * @see pause
     * @see resume
     * @see stop
     */
    // Playback
    ExpectedVoid play(int64_t queueId = 1);

    /**
     * @brief Pauses playback.
     * @ingroup caudio_engine
     * @return Success or Error State if not playing.
     * @par Thread safety
     * Thread-safe; atomics only.
     */
    ExpectedVoid pause();

    /**
     * @brief Resumes from paused.
     * @ingroup caudio_engine
     * @return Success or Error State if not paused.
     * @par Thread safety
     * Thread-safe; notifies decode/monitor cvs.
     */
    ExpectedVoid resume();

    /**
     * @brief Stops playback and resets position/ring.
     * @ingroup caudio_engine
     * @return Success (always).
     * @par Thread safety
     * Thread-safe.
     * @see play
     */
    ExpectedVoid stop();

    /**
     * @brief Seeks to a position in the current track.
     * @ingroup caudio_engine
     * @param seconds Target position in seconds [0, duration].
     * @return Success or Error State/InvalidArg/Internal.
     * @retval StatusCode::State if no current track.
     * @retval StatusCode::InvalidArg if seconds is NaN/inf/negative.
     * @retval StatusCode::Internal if decoder seek fails (state restored).
     * @details Holds decodeMtx_ to serialize with decodeLoop (decoder_->decode
     * / ring_->write). Temporarily Pauses, snapshots pausePos_/playStart_ so
     * a failed seek restores the prior state and resumes Playing.
     * @par Thread safety
     * Thread-safe; locks decodeMtx_.
     */
    ExpectedVoid seek(double seconds);

    /**
     * @brief Sets playback volume and persists it.
     * @ingroup caudio_engine
     * @param g Volume in [0,1] (clamped; NaN/inf -> 0).
     * @return Success (always).
     * @details Updates state_.volume, volume_ atomically and forwards to
     * AudioOutput::setVolume; persists via saveState() if DB attached.
     * @par Thread safety
     * Thread-safe; atomic volume + saveState transaction.
     */
    ExpectedVoid setVolume(float g);

    /** @brief Returns current volume [0,1]. @ingroup caudio_engine
     * @return Volume level (atomic load, relaxed). */
    float volume() const noexcept;
    /** @brief Returns the unmute restore level (0,1]. @ingroup caudio_engine
     * @details Last non-zero level before mute (persisted pre_mute_volume).
     * @return Restore level (atomic load, relaxed). */
    float preMuteVolume() const noexcept;
    /** @brief Sets the unmute restore level (sanitized, persisted on next save).
     * @ingroup caudio_engine
     * @param g Level in (0,1]; anything else becomes 0.5.
     * @details Callers follow with setVolume(), which persists via saveState().
     * @par Thread safety
     * Thread-safe; atomic store. */
    ExpectedVoid setPreMuteVolume(float g);
    /** @brief Returns current playback state. @ingroup caudio_engine
     * @return PlaybackState (atomic load, acquire). */
    PlaybackState state() const noexcept;
    /** @brief Returns current track duration in seconds. @ingroup caudio_engine
     * @return Duration (cached from decoder or metadata). */
    double duration() const noexcept;
    /** @brief Returns current playback position in seconds. @ingroup caudio_engine
     * @return Position (0 if no track; pausePos_ if paused; pausePos_ + elapsed if playing,
     * clamped). */
    double position() const noexcept;
    /** @brief Returns current track id (or persisted one if no current). @ingroup caudio_engine
     * @return Track id if hasCurrent_, else state_.currentTrackId (persisted). */
    int64_t currentTrackId() const noexcept;

    /** @brief Returns whether shuffle is enabled. @ingroup caudio_engine
     * @return True if shuffle mode active. */
    bool shuffle() const noexcept;
    /** @brief Returns the shuffle permutation (positions) for a queue.
     * @ingroup caudio_engine
     * @param qid Queue id.
     * @return Perm copy when shuffle applies to qid, else empty (insertion order).
     * Empty on lock contention too (callers fall back to insertion order).
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_. */
    std::vector<int64_t> shufflePermFor(int64_t qid) noexcept;
    /** @brief Extends the shuffle permutation after tracks were appended.
     * @ingroup caudio_engine
     * @param qid Queue id the tracks were appended to.
     * @param count Number of appended tracks (at the queue end).
     * @details Best-effort: no-op unless shuffle applies to qid or the queue
     * lock is contended (stale-perm fallbacks cover display and playback).
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_. */
    void noteEnqueued(int64_t qid, std::size_t count) noexcept;

    /** @brief Returns current repeat mode. @ingroup caudio_engine
     * @return RepeatMode (Off/All/One). */
    RepeatMode repeat() const noexcept;

    /**
     * @brief Returns database statistics.
     * @ingroup caudio_engine
     * @return `std::expected<DbStats, Error>` -- stats on success, State if no db.
     * @par Thread safety
     * Thread-safe; delegates to Database::getStats() which takes shared_lock.
     * @see caudio::db::Database::getStats
     * @see caudio::db::DbStats
     */
    std::expected<caudio::db::DbStats, caudio::utils::Error> getStats();

    /**
     * @brief Lists history entries.
     * @ingroup caudio_engine
     * @param limit Max rows (0 = no limit, default 50).
     * @return `std::expected<std::vector<db::HistoryEntry>, Error>` -- vector on success, State if
     * no db. Entries carry track snapshots (title/artist/path/duration).
     * @par Thread safety
     * Thread-safe; takes shared_lock on the database mutex.
     * @see caudio::db::HistoryEntry
     */
    std::expected<std::vector<caudio::db::HistoryEntry>, caudio::utils::Error>
    listHistory(int limit = 50);

    /**
     * @brief Clears all history entries.
     * @ingroup caudio_engine
     * @return `std::expected<void, Error>` -- success or State/Internal.
     * @par Thread safety
     * Thread-safe; takes unique_lock on the database mutex.
     */
    std::expected<void, caudio::utils::Error> clearHistory();

    /**
     * @brief Returns last decoder/output error string.
     * @ingroup caudio_engine
     * @return Error message or empty string if none.
     * @par Thread safety
     * Thread-safe; reads lastErr_ (written from doPlayTrack, not atomic but single-writer).
     * @see doPlayTrack
     */
    std::string lastError() const;

    /**
     * @brief Enables or disables shuffle.
     * @ingroup caudio_engine
     * @param on True to enable, false to disable.
     * @return Success or Error State/Busy/Internal.
     * @retval StatusCode::State if no db.
     * @retval StatusCode::Busy if queueMutex_ try_lock fails.
     * @details Delegates to setShuffleLocked under queueMutex_, then
     * pushes QueueChanged event.
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_.
     * @see setShuffleLocked
     */
    ExpectedVoid setShuffle(bool on);

    /**
     * @brief Sets repeat mode and persists it.
     * @ingroup caudio_engine
     * @param m RepeatMode (Off/All/One).
     * @return Success or Error InvalidArg/Busy/Internal.
     * @retval StatusCode::InvalidArg if m is out of range.
     * @details Under queueMutex_: updates queue_.repeat/state_.repeatMode
     * and persists via saveState() (handles active_queue_id migration).
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_.
     */
    ExpectedVoid setRepeat(RepeatMode m);

    /**
     * @brief Returns the active queue id.
     * @ingroup caudio_engine
     * @return queue_.queue_id if set, else state_.activeQueueId, else 1 (atomic under queueMutex_).
     * @par Thread safety
     * Thread-safe; locks queueMutex_.
     * @see switchQueue
     */
    int64_t activeQueueId() const noexcept;

    /**
     * @brief Switches the active queue.
     * @ingroup caudio_engine
     * @param qid Queue id to switch to (0 defaults to 1).
     * @return Success or Error State/Busy/NotFound.
     * @retval StatusCode::State if no db.
     * @retval StatusCode::Busy if queueMutex_ try_lock fails.
     * @details Validates via db_->getQueue(qid) while holding
     * queueMutex_ (then dbMutex_ shared). On switch: resets cursor to 0,
     * clears perm (shuffle flag kept but perm regenerated on next shuffle),
     * updates state_.activeQueueId/cursorPos and persists via saveState().
     * Pushes QueueChanged event.
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_.
     */
    ExpectedVoid switchQueue(int64_t qid);

    /**
     * @brief Advances to the next track per RepeatMode/shuffle.
     * @ingroup caudio_engine
     * @return Success or Error State/Busy/NotFound/Device.
     * @details Fast-path: if !shuffle && repeat==One && hasCurrent, seeks
     * decoder to 0 under decodeMtx_ and resets ring without touching the
     * queue. Otherwise locks queueMutex_ try_lock, calls queueNextLocked
     * (which handles shuffle perm, wrap/reshuffle and RepeatMode::One),
     * then doPlayTrack.
     * @par Thread safety
     * Thread-safe; fast-path locks decodeMtx_, otherwise queueMutex_.
     * @see queueNextLocked
     * @see doPlayTrack
     */
    ExpectedVoid next();

    /**
     * @brief Monitor auto-advance: like next(), but halts at queue end.
     * @ingroup caudio_engine
     * @return Success or Error State/Busy/NotFound/Device.
     * @details With repeat Off and the cursor past the end, stops playback
     * (cursor stays parked so a later play wraps to the head) instead of
     * wrapping like next(). Repeat One/All behave like next().
     * @par Thread safety
     * Thread-safe; same locking as next().
     * @see next
     */
    ExpectedVoid autoNext();

    /**
     * @brief Moves to the previous track.
     * @ingroup caudio_engine
     * @return Success or Error State/Busy/NotFound/Device ("at start"/"no perm").
     * @details Locks queueMutex_ try_lock then queuePrevLocked + doPlayTrack.
     * Shuffle path steps cursor back by 2 and clamps; non-shuffle mirrors.
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_.
     * @see queuePrevLocked
     */
    ExpectedVoid prev();

    /**
     * @brief Sets runtime callbacks.
     * @ingroup caudio_engine
     * @param cbs New EngineCallbacks struct (copied).
     * @return Success (always).
     * @par Thread safety
     * Thread-safe; locks cbMutex_.
     * @see EngineCallbacks
     * @see pushEvent
     */
    ExpectedVoid setCallbacks(const EngineCallbacks& cbs);

    /**
     * @brief Pops one event from the MPSC queue (non-blocking).
     * @ingroup caudio_engine
     * @return `std::expected<EngineEvent, Error>` -- event on success, NotFound if queue empty.
     * @par Thread safety
     * Thread-safe; MpscQueue is lock-free MPSC.
     * @see drainEvents
     * @see drainAll
     * @see EngineEvent
     */
    std::expected<EngineEvent, caudio::utils::Error> pollEvent();

    /**
     * @brief Drains up to cap events into a caller-provided buffer.
     * @ingroup caudio_engine
     * @param buf Output buffer (may be null if cap==0).
     * @param cap Capacity of buf.
     * @param n Out: number of events written (must be non-null).
     * @return `std::expected<void, Error>` -- success or InvalidArg if n is null or buf is null
     * with cap>0.
     * @par Thread safety
     * Thread-safe; pops from MpscQueue (lock-free).
     * @see pollEvent
     * @see drainAll
     * @see EngineEvent
     */
    ExpectedVoid drainEvents(EngineEvent* buf, size_t cap, size_t* n);

    /**
     * @brief Drains all queued events into a vector.
     * @ingroup caudio_engine
     * @return Vector of events (may be empty).
     * @par Thread safety
     * Thread-safe; pops from MpscQueue (lock-free).
     * @see pollEvent
     * @see drainEvents
     * @see EngineEvent
     */
    std::vector<EngineEvent> drainAll();

  private:
    /**
     * @brief Constructs Engine with config defaults applied.
     * @ingroup caudio_engine
     * @param cfg EngineConfig; zero poll/gapless/history values are replaced by defaults.
     * @details Applies defaults: pollMs 10, gaplessMs 300, historyThresholdPct 60,
     * historyThresholdSecs 90; initializes atomics and copies callbacks.
     */
    explicit Engine(const EngineConfig& cfg);

    /**
     * @brief Starts decode and (optionally) monitor threads.
     * @ingroup caudio_engine
     * @return std::nullopt on success.
     * @details Sets decodeRun_/monRun_, spawns jthreads running
     * decodeLoop/monitorLoop. Called from create()/open().
     * @par Thread safety
     * Called during construction before shared access.
     */
    std::optional<caudio::utils::Error> init();

    /** @brief Returns true if DB handle is attached. @ingroup caudio_engine */
    bool hasDb() const noexcept;

    /**
     * @brief Tries to acquire queueMutex_ without blocking.
     * @ingroup caudio_engine
     * @return true if lock acquired; false means Busy.
     */
    bool tryLockQueue() noexcept;
    /** @brief Releases queueMutex_. @ingroup caudio_engine */
    void unlockQueue() noexcept;

    /**
     * @brief Computes current playback position in seconds.
     * @ingroup caudio_engine
     * @return Position; 0 if no current track; pausePos_ if Paused/Stopped,
     * otherwise pausePos_ + elapsed since playStart_ clamped to duration.
     * @par Thread safety
     * Lock-free; reads atomics.
     */
    double currentPositionLocked() const noexcept;

    // State persistence
    /**
     * @brief Returns borrowed sqlite3* handle or nullptr if no database.
     * @ingroup caudio_engine
     * @return Raw sqlite3 pointer (owned by Database) or nullptr.
     * @par Thread safety
     * Thread-safe; reads shared_ptr atomically.
     */
    sqlite3* dbHandle() const noexcept;
    /**
     * @brief Returns pointer to Database mutex or nullptr if no database.
     * @ingroup caudio_engine
     * @return Pointer to shared_mutex (owned by Database) or nullptr.
     * @par Thread safety
     * Thread-safe; reads shared_ptr atomically.
     */
    std::shared_mutex* dbMutex() const noexcept;

    /**
     * @brief Executes a callable inside a SQLite BEGIN IMMEDIATE / COMMIT transaction.
     * @ingroup caudio_engine
     * @param fn Transaction body; receives the database handle.
     * @return `std::expected<void, Error>` -- success or error (Busy if begin fails, Internal on
     * commit failure, InvalidArg if no db).
     * @details Acquires unique_lock on dbMutex_, executes `BEGIN IMMEDIATE`, runs `fn(h)`,
     * commits on success, rolls back on any failure. Used by all state persistence methods.
     * @par Thread safety
     * Locks dbMutex_ exclusively; callers must not hold queueMutex_ to avoid deadlock.
     */
    std::expected<void, caudio::utils::Error>
    withTransaction(std::function<std::expected<void, caudio::utils::Error>(struct sqlite3*)> fn);

    /**
     * @brief Loads persisted engine state from engine_state row id=1.
     * @ingroup caudio_engine
     * @return `std::optional<Error>` -- `std::nullopt` on success (including no row, defaults
     * applied), or Error on failure.
     * @details Tries `sqlNew` (with `active_queue_id` column) first; if prepare fails (old schema),
     * falls back to `sqlOld` without that column for migration. On row found:
     * - Restores EngineState fields: shuffle_enabled, repeat_mode, cursor_pos, current_track_id,
     * volume, active_queue_id.
     * - Deserializes `shuffle_perm` BLOB into `queue_.perm` if shuffleEnabled.
     * - Validates `active_queue_id` (>=1, checks queues table) and clamps cursor to permutation
     * size. On SQLITE_DONE (no row): resets all state to defaults (shuffle=off, repeat=Off,
     * cursor=0, volume=1.0, queue_id=1). Holds dbMutex_ unique_lock throughout.
     * @par Thread safety
     * Locks dbMutex_ exclusively; callers may hold queueMutex_ externally but not required.
     * @see saveState
     * @see persistShuffleBlobLocked
     * @see persistCursorLocked
     */
    std::optional<caudio::utils::Error> loadState();

    /**
     * @brief Persists current engine state to engine_state row id=1.
     * @ingroup caudio_engine
     * @return `std::expected<void, Error>` -- success or error (Internal on prepare/step failure,
     * NoMem if perm too large).
     * @details Uses `withTransaction()` for atomicity. Tries `sqlNew` (with `active_queue_id`)
     * first; if prepare fails (old schema), falls back to `sqlOld`. Binds:
     * - shuffle_enabled, repeat_mode, shuffle_perm (BLOB or NULL), cursor_pos,
     *   current_track_id, volume, active_queue_id (or queue_.queue_id fallback).
     * Handles `active_queue_id` migration: writes new column if schema supports it.
     * @par Thread safety
     * Locks dbMutex_ exclusively via withTransaction(); callers typically hold queueMutex_.
     * @see loadState
     * @see withTransaction
     */
    std::expected<void, caudio::utils::Error> saveState();

    /**
     * @brief Persists shuffle permutation and cursor position atomically.
     * @ingroup caudio_engine
     * @return `std::expected<void, Error>` -- success or error (Internal on prepare/step failure,
     * NoMem if perm too large).
     * @details Executes `UPDATE engine_state SET shuffle_perm=?, cursor_pos=?, shuffle_enabled=?
     * WHERE id=1` inside a transaction. Binds the current `queue_.perm` as BLOB (or NULL if not
     * shuffling), `queue_.cursor`, and `queue_.shuffle`. Also updates `state_.shuffleEnabled` and
     * `state_.cursorPos`. Called by `setShuffleLocked`, `queueNextLocked`, `queuePrevLocked` after
     * modifying shuffle/cursor.
     * @par Thread safety
     * Caller must hold `queueMutex_`; locks dbMutex_ exclusively via withTransaction().
     * @see setShuffleLocked
     * @see queueNextLocked
     * @see queuePrevLocked
     * @see withTransaction
     */
    std::expected<void, caudio::utils::Error> persistShuffleBlobLocked();

    /**
     * @brief Persists only the cursor position to engine_state.
     * @ingroup caudio_engine
     * @return `std::expected<void, Error>` -- success or error (Internal on prepare/step failure).
     * @details Executes `UPDATE engine_state SET cursor_pos=? WHERE id=1` inside a transaction.
     * Binds `queue_.cursor`. Also updates `state_.cursorPos`. Lighter than
     * persistShuffleBlobLocked; used when only cursor advances (non-shuffle next/prev).
     * @par Thread safety
     * Caller must hold `queueMutex_`; locks dbMutex_ exclusively via withTransaction().
     * @see persistShuffleBlobLocked
     * @see withTransaction
     */
    std::expected<void, caudio::utils::Error> persistCursorLocked();

    /**
     * @brief Fetches track from queue at given position (under db shared_lock).
     * @ingroup caudio_engine
     * @param qid Queue id (0 defaults to 1).
     * @param pos Zero-based position in queue.
     * @return `std::expected<Track, Error>` -- track on success, NotFound if position out of range,
     * Internal on SQL error.
     * @details Executes `SELECT track_id FROM queue WHERE queue_id=? ORDER BY position LIMIT 1
     * OFFSET ?` under dbMutex_ shared_lock, then calls `db_->getTrack(trackId)`.
     * @par Thread safety
     * Locks dbMutex_ shared; caller typically holds queueMutex_.
     * @see queueNextLocked
     * @see queuePrevLocked
     */
    std::expected<caudio::db::Track, caudio::utils::Error> fetchTrackByPosLocked(int64_t qid,
                                                                                 int64_t pos);

    /**
     * @brief Internal shuffle toggle with persistence (queueMutex_ must be held).
     * @ingroup caudio_engine
     * @param on True to enable shuffle, false to disable.
     * @return `std::expected<void, Error>` -- success or error from persistShuffleBlobLocked.
     * @details If enabling and perm empty: clears perm/cursor, gets queue count, generates new
     * random permutation (random_device-seeded), persists via
     * persistShuffleBlobLocked. If disabling: clears perm, sets shuffle=false, persists. No-op if
     * state unchanged and perm non-empty. Updates `state_.shuffleEnabled` on success.
     * @par Thread safety
     * Caller MUST hold `queueMutex_` (acquired via tryLockQueue()). Locks dbMutex_ via
     * persistShuffleBlobLocked.
     * @see setShuffle
     * @see persistShuffleBlobLocked
     */
    std::expected<void, caudio::utils::Error> setShuffleLocked(bool on);

    /**
     * @brief Advances queue cursor to next track per shuffle/repeat mode (queueMutex_ held).
     * @ingroup caudio_engine
     * @param out Output track reference (filled on success).
     * @return `std::expected<void, Error>` -- success, NotFound if queue empty, Internal on fetch
     * error.
     * @details Shuffle path:
     * - If perm empty, calls setShuffleLocked(true) to generate.
     * - If cursor >= perm.size():
     *   - RepeatMode::One: re-plays last perm index (cursor-1 or last).
     *   - Off/Queue: calls setShuffleLocked(true) to reshuffle, resets cursor=0, persists cursor.
     * - Returns track at perm[cursor], increments cursor, persists cursor.
     * Non-shuffle path:
     * - If cursor >= count:
     *   - RepeatMode::One: re-plays last index (cursor-1 or last).
     *   - Off/Queue: wraps cursor to 0 (reshuffles if shuffle on), persists cursor.
     * - Returns track at cursor, increments cursor, persists cursor.
     * @par Thread safety
     * Caller MUST hold `queueMutex_` (acquired via tryLockQueue()). Locks dbMutex_ shared for
     * fetch, exclusive via persistCursorLocked/persistShuffleBlobLocked.
     * @see next
     * @see queuePrevLocked
     * @see setShuffleLocked
     * @see persistCursorLocked
     * @see persistShuffleBlobLocked
     */
    std::expected<void, caudio::utils::Error> queueNextLocked(caudio::db::Track& out);

    /**
     * @brief Shared next/autoNext body (queueMutex_ NOT held on entry).
     * @ingroup caudio_engine
     * @param stopAtEnd When true with repeat Off, halts at queue end instead
     * of wrapping (cursor stays parked past the end).
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_.
     * @see next
     * @see autoNext
     */
    ExpectedVoid advanceLocked(bool stopAtEnd);

    /**
     * @brief Moves queue cursor to previous track per shuffle/repeat mode (queueMutex_ held).
     * @ingroup caudio_engine
     * @param out Output track reference (filled on success).
     * @return `std::expected<void, Error>` -- success, NotFound if queue empty/at start/no perm.
     * @details Non-shuffle path:
     * - If cursor <= 1: returns NotFound ("at start" if cursor==0).
     * - Else: cursor -= 2, clamp to count-1, fetch track at cursor, increment cursor, persist
     * cursor. Shuffle path:
     * - If perm empty: NotFound ("no perm").
     * - If cursor <= 1: cursor=0 (or NotFound if cursor==0).
     * - Else: cursor -= 2, clamp to perm.size()-1, fetch track at perm[cursor], increment cursor,
     * persist cursor.
     * @par Thread safety
     * Caller MUST hold `queueMutex_` (acquired via tryLockQueue()). Locks dbMutex_ shared for
     * fetch, exclusive via persistCursorLocked.
     * @see prev
     * @see queueNextLocked
     * @see persistCursorLocked
     */
    std::expected<void, caudio::utils::Error> queuePrevLocked(caudio::db::Track& out);

    /**
     * @brief Initializes decoder/reader/ring/output for a track and starts playback.
     * @ingroup caudio_engine
     * @param t Track to play (path used to open reader/decoder).
     * @return `std::expected<void, Error>` -- success, or NotFound for an
     * unplayable track file / Device for audio-device failure (both gated
     * behind allowSimulatedPlayback, which falls back to timer playback).
     * @details Resets prior decoder/reader/ring/output. Attempts to open FileReader +
     * Decoder::open from track path. On success: sets duration_, creates SpscRing (8192*ch
     * frames), AudioOutput, calls preroll() to fill ring to half capacity, starts output. On
     * failure without allowSimulatedPlayback: returns an error and leaves
     * playback stopped (no state change, no TrackStarted event); with it:
     * falls back to metadata duration (no audio output), sets lastErr_. Updates state:
     * currentTrack_, hasCurrent_=true, markedPlayed_=false, gaplessArmed_=false,
     * startedMs_=nowMs(), state_.currentTrackId, cursorPos, playbackState_=Playing, playStart_=now,
     * pausePos_=0. Calls saveState() if DB attached. Pushes TrackStarted event. Notifies decodeCv_
     * and monCv_.
     * @par Thread safety
     * Called with queueMutex_ held (from play/next/prev). Locks decodeMtx_ indirectly via
     * output/decoder creation (no concurrent decodeLoop access yet). Must not be called from
     * decodeLoop/monitorLoop.
     * @see play
     * @see next
     * @see prev
     * @see preroll
     * @see saveState
     * @see pushEvent
     */
    std::expected<void, caudio::utils::Error> doPlayTrack(caudio::db::Track& t);

    /**
     * @brief Pre-fills the SPSC ring to half capacity before starting audio output.
     * @ingroup caudio_engine
     * @details Called from doPlayTrack after creating ring and output. Decodes frames in
     * chunks (up to 1024 frames, max 2048/ch) until ring is half full or EOF. Uses
     * decoder_->decode() into a temporary buffer, writes to ring_. Early exit if ring
     * cannot accept a full chunk. Ensures gapless transition by having audio ready
     * immediately on output_->start().
     * @par Thread safety
     * Called from doPlayTrack with queueMutex_ held; decodeLoop not yet running for
     * this track. No locks needed.
     * @see doPlayTrack
     * @see decodeLoop
     */
    void preroll();

    /**
     * @brief Pushes event to MPSC queue and dispatches callbacks.
     * @ingroup caudio_engine
     * @param ev EngineEvent to enqueue.
     * @details Pushes to eventQueue_ (capacity 64, drop-on-full). On success, copies callbacks
     * under cbMutex_ and dispatches synchronously: TrackStarted -> on_track_started,
     * TrackEnded -> on_track_ended (with completion % calc), QueueChanged -> on_queue_changed,
     * Error -> on_error.
     * @par Thread safety
     * Thread-safe; eventQueue_ is lock-free MPSC. cbMutex_ protects callback copy.
     * Called from: play/next/prev/doPlayTrack (TrackStarted), engineTick (Progress/TrackEnded via
     * gapless), setShuffle/switchQueue (QueueChanged).
     * @see eventQueue_
     * @see EngineCallbacks
     * @see EngineEventType
     */
    void pushEvent(const EngineEvent& ev);

    /**
     * @brief Marks current track as played in history if thresholds met (exactly-once via CAS).
     * @ingroup caudio_engine
     * @details Called from engineTick(). Early exits if: no DB, no current track, already marked.
     * Checks the history thresholds from config (historyThresholdPct default 60,
     * historyThresholdSecs default 90).
     * Uses atomic CAS on markedPlayed_ (false->true) for exactly-once semantics.
     * Inside transaction (BEGIN IMMEDIATE): fetches track play_count, updates tracks
     * (play_count+1, last_played=now), inserts into history (track_id, started_at,
     * completed_at, position_ms, completion_pct, queue_id). Rolls back on any failure,
     * resets markedPlayed_=false. On commit success, markedPlayed_ remains true.
     * @par Thread safety
     * Called from monitorLoop (single-threaded). Locks dbMutex_ exclusively.
     * @see engineTick
     * @see monitorLoop
     */
    void doHistoryMark();

    /**
     * @brief Periodic tick: history marking, progress events, gapless transition.
     * @ingroup caudio_engine
     * @details Called from monitorLoop every pollMs (default 10 ms).
     * 1. Calls doHistoryMark() to persist history if thresholds met.
     * 2. If Playing: emits Progress event every 500 ms (track_id, queue_id, position, duration).
     * 3. If Playing and duration_ > 0: computes remaining = duration - position; if
     *    remaining <= gaplessMs/1000 (default 300 ms) and >= 0: attempts CAS on
     *    gaplessArmed_ (0->1). On success: calls next() to queue/start next track;
     *    on next() failure, resets gaplessArmed_=false. This arms gapless ~300ms
     *    before track end so decodeLoop can preroll the next track.
     * @par Thread safety
     * Called from monitorLoop (single-threaded). Reads atomics (playbackState_, hasCurrent_,
     * lastProgressMs_, gaplessArmed_). Calls next() which locks queueMutex_.
     * @see monitorLoop
     * @see doHistoryMark
     * @see next
     * @see gaplessArmed_
     */
    void engineTick();

    /**
     * @brief Monitor thread main loop -- polls engineTick at configurable interval.
     * @ingroup caudio_engine
     * @param st Stop token from jthread (request_stop() on shutdown).
     * @details Runs while monRun_ is true and stop not requested. Uses monMtx_/monCv_ to wait
     * for pollMs (default 10 ms) or notification. Each iteration calls engineTick() to:
     * - mark history, emit progress, arm gapless transition.
     * Poll interval set by EngineConfig::pollMs (min 1 ms). Lock released during engineTick()
     * to minimize contention.
     * @par Thread safety
     * Runs on dedicated monitorThread_ (jthread). Single writer for engineTick().
     * Waits on monCv_ with predicate checking stop token and monRun_.
     * @see engineTick
     * @see init
     * @see shutdown
     * @see EngineConfig::pollMs
     */
    void monitorLoop(std::stop_token st);

    /**
     * @brief Decode thread main loop -- fills SPSC ring with decoded audio frames.
     * @ingroup caudio_engine
     * @param st Stop token from jthread (request_stop() on shutdown).
     * @details Runs while decodeRun_ is true and stop not requested.
     * - If not Playing or no decoder/ring: waits on decodeCv_ (10 ms timeout) for state change.
     * - If ring full: sleeps 5 ms and retries.
     * - Computes max frames to decode (avail/ch, capped at 1024 frames, max 2048/ch).
     * - Decodes under decodeMtx_ (re-checks Playing state to avoid race with seek()).
     * - On EOF (frames==0): sleeps 10 ms, lets monitorLoop handle gapless/next.
     * - Writes decoded frames to ring under decodeMtx_ (re-checks Playing to avoid race with seek()
     * ring reset). SPSC ring (ring_) is written by decodeLoop only; read by AudioOutput callback.
     * @par Thread safety
     * Runs on dedicated decodeThread_ (jthread). Single writer to ring_.
     * Uses decodeMtx_ to serialize with seek()/doPlayTrack() decoder/ring access.
     * Waits on decodeCv_ notified by play/pause/resume/stop/seek/next/prev.
     * @see init
     * @see shutdown
     * @see decodeMtx_
     * @see decodeCv_
     * @see caudio::utils::SpscRing
     * @see caudio::player::Decoder
     */
    void decodeLoop(std::stop_token st);

    // members
    EngineConfig cfg_{};
    EngineState state_{};
    QueueState queue_{};
    caudio::db::Track currentTrack_{};
    std::atomic<bool> hasCurrent_{false};
    double duration_{0};
    std::atomic<float> volume_{1.0f};
    std::atomic<float> preMute_{0.5f};
    std::atomic<PlaybackState> playbackState_{PlaybackState::Stopped};
    std::chrono::steady_clock::time_point playStart_{};
    double pausePos_{0};
    std::atomic<bool> markedPlayed_{false};
    int64_t startedMs_{0};
    std::string lastErr_{};

    std::shared_ptr<caudio::db::Database> db_{};

    // player
    std::unique_ptr<caudio::player::Reader> reader_{};
    std::unique_ptr<caudio::player::Decoder> decoder_{};
    std::unique_ptr<caudio::utils::SpscRing<float>> ring_{};
    std::unique_ptr<caudio::player::AudioOutput> output_{};

    std::jthread decodeThread_{};
    std::atomic<bool> decodeRun_{false};
    std::mutex decodeMtx_{};
    std::condition_variable_any decodeCv_{};

    // monitor
    std::jthread monitorThread_{};
    std::atomic<bool> monRun_{false};
    std::mutex monMtx_{};
    std::condition_variable_any monCv_{};

    // events
    caudio::utils::MpscQueue<EngineEvent> eventQueue_{64};
    std::mutex cbMutex_{};
    EngineCallbacks callbacks_{};

    // atomics spec required
    std::atomic<uint64_t> lastProgressMs_{0};
    // gaplessArmed_: 0->1 CAS arms gapless pre-roll ~300ms before track end (gaplessMs).
    // Reset to false on TrackStarted / next() failure. Requires engineTick() single-writer.
    std::atomic<bool> gaplessArmed_{false};
    mutable std::mutex queueMutex_;
};

} // namespace caudio::engine
