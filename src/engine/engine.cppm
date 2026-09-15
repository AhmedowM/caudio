module;
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/**
 * @file engine.cppm
 * @brief Playback engine — state machine, gapless, decode/monitor loops and persistence.
 * @ingroup caudio_engine
 * @details Aggregate module `caudio.engine` re-exporting `:types`, `:history`
 * and `:shuffle`. Core class is Engine which owns:
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
 * - History: doHistoryMark uses shouldMarkPlayedEx(historyThresholdPct/Secs)
 *   with atomic CAS on markedPlayed_ and a BEGIN IMMEDIATE transaction.
 * Locking: queueMutex_ (mutex, try_lock) for QueueState; decodeMtx_
 * (mutex) for decoder_/ring_ vs seek/decodeLoop; queue/state DB writes
 * go through Database::mutex() (shared_mutex) + withTransaction.
 * Events: MpscQueue<EngineEvent,64> with drop-on-full, dispatched via
 * pushEvent to callbacks under cbMutex_.
 */

export module caudio.engine;

export import :types;
export import :history;
export import :shuffle;

import caudio.utils;
import caudio.player;
import caudio.db;

export namespace caudio::engine {

/**
 * @brief RAII guard for sqlite3_exec error strings.
 * @ingroup caudio_engine
 * @details Frees the `char*` returned by `sqlite3_exec` on scope exit.
 */
struct SqliteErrGuard {
    char* p; ///< Owned error string from sqlite3_exec.
    ~SqliteErrGuard() {
        if (p)
            sqlite3_free(p);
    }
};

/**
 * @brief RAII guard for sqlite3_stmt.
 * @ingroup caudio_engine
 * @details Finalizes the statement on scope exit. Non-copyable.
 */
struct StmtGuard {
    sqlite3_stmt* s = nullptr;
    explicit StmtGuard(sqlite3_stmt* stmt) : s(stmt) {}
    ~StmtGuard() {
        if (s)
            sqlite3_finalize(s);
    }
    StmtGuard(const StmtGuard&) = delete;
    StmtGuard& operator=(const StmtGuard&) = delete;
    sqlite3_stmt* get() const noexcept {
        return s;
    }
    sqlite3_stmt* operator->() const noexcept {
        return s;
    }
};

/**
 * @brief Forwards StatusCode to string via caudio::utils::toString.
 * @ingroup caudio_engine
 * @param r Status code.
 * @return String view from utils::toString.
 */
constexpr std::string_view toString(caudio::utils::StatusCode r) noexcept {
    return caudio::utils::toString(r);
}

/**
 * @brief Main playback engine — queue, decoding, gapless, history and persistence.
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
    using ExpectedEngine = std::expected<std::unique_ptr<Engine>, caudio::utils::Error>; ///< Engine ptr or Error.

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
    static ExpectedEngine create(const EngineConfig& cfg = {}) {
        auto e = std::unique_ptr<Engine>(new Engine(cfg));
        auto err = e->init();
        if (err)
            return std::unexpected(*err);
        return e;
    }

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
    static ExpectedEngine open(std::string_view path, const EngineConfig& cfg = {}) {
        auto dbRes = caudio::db::Database::open(path);
        if (!dbRes)
            return std::unexpected(dbRes.error());
        auto e = std::unique_ptr<Engine>(new Engine(cfg));
        e->db_ = std::shared_ptr<caudio::db::Database>(std::move(dbRes.value()));
        if (auto err = e->init())
            return std::unexpected(*err);
        (void)e->loadState();
        if (e->output_)
            e->output_->setVolume(e->state_.volume);
        return e;
    }

    /**
     * @brief Attaches an already-open Database.
     * @ingroup caudio_engine
     * @param db Shared Database handle (must be non-null and open).
     * @return Success or Error InvalidArg / loadState failure.
     * @retval StatusCode::InvalidArg if db is null or handle is null.
     * @par Thread safety
     * Thread-safe; calls loadState() under dbMutex_.
     * @see attachDb
     * @see loadState
     */
    ExpectedVoid attachDatabase(std::shared_ptr<caudio::db::Database> db) {
        if (!db || !db->handle())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "null db"));
        db_ = std::move(db);
        if (auto ec = loadState())
            return std::unexpected(*ec);
        return {};
    }
    /**
     * @brief Attaches a unique Database handle (compat).
     * @ingroup caudio_engine
     * @param db Owning handle to move into shared_ptr.
     * @return Success or Error InvalidArg.
     * @details Wraps the unique_ptr into shared_ptr then delegates to attachDatabase.
     * @par Thread safety
     * Thread-safe.
     */
    ExpectedVoid attachDb(std::unique_ptr<caudio::db::Database> db) {
        if (!db || !db->handle())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "null db"));
        auto shared = std::shared_ptr<caudio::db::Database>(std::move(db));
        return attachDatabase(std::move(shared));
    }

    /** @brief Tears down threads and persists state. @ingroup caudio_engine */
    ~Engine() {
        shutdown();
    }

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
    void shutdown() {
        // stop monitor
        monRun_.store(false, std::memory_order_release);
        monCv_.notify_all();
        if (monitorThread_.joinable()) {
            monitorThread_.request_stop();
            monCv_.notify_all();
            monitorThread_.join();
        }
        // stop decode
        decodeRun_.store(false, std::memory_order_release);
        decodeCv_.notify_all();
        if (decodeThread_.joinable()) {
            decodeThread_.request_stop();
            decodeCv_.notify_all();
            decodeThread_.join();
        }
        // persist state
        if (db_ && db_->handle()) {
            state_.cursorPos = (int64_t)queue_.cursor;
            (void)saveState();
        }
        if (output_) {
            output_->stop();
            output_.reset();
        }
        decoder_.reset();
        reader_.reset();
        ring_.reset();
    }

    /**
     * @brief Starts or resumes playback.
     * @ingroup caudio_engine
     * @param queueId Active queue id (0 defaults to 1).
     * @return Success or Error State/Busy/NotFound/Internal.
     * @retval StatusCode::State if no db.
     * @retval StatusCode::Busy if queueMutex_ try_lock fails.
     * @retval StatusCode::NotFound if queue empty.
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
    ExpectedVoid play(int64_t queueId = 1) {
        if (!hasDb())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
        if (queueId == 0)
            queueId = 1;
        auto s = playbackState_.load(std::memory_order_acquire);
        if (s == PlaybackState::Paused) {
            // resume, don't dequeue
            playStart_ = std::chrono::steady_clock::now();
            playbackState_.store(PlaybackState::Playing, std::memory_order_release);
            if (output_)
                output_->start();
            decodeCv_.notify_all();
            monCv_.notify_all();
            return {};
        }
        if (s == PlaybackState::Playing) {
            // already playing, restart current track (seek 0), don't dequeue
            // serialize with decodeLoop (decoder_->decode / ring_->write) via decodeMtx_
            std::unique_lock<std::mutex> lk(decodeMtx_);
            if (decoder_) {
                auto r = decoder_->seek(0);
                if (!r)
                    return std::unexpected(r.error());
            }
            if (ring_)
                ring_->reset();
            pausePos_ = 0;
            playStart_ = std::chrono::steady_clock::now();
            if (output_)
                output_->start();
            lk.unlock();
            decodeCv_.notify_all();
            monCv_.notify_all();
            return {};
        }
        // Stopped -> start new track via cursor (not dequeue)
        if (!tryLockQueue())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Busy, "queue busy"));
        queue_.queue_id = queueId;
        caudio::db::Track t;
        auto r = queueNextLocked(t);
        if (!r) {
            unlockQueue();
            return std::unexpected(r.error());
        }
        auto pr = doPlayTrack(t);
        unlockQueue();
        if (!pr)
            return std::unexpected(pr.error());
        return {};
    }

    /**
     * @brief Pauses playback.
     * @ingroup caudio_engine
     * @return Success or Error State if not playing.
     * @par Thread safety
     * Thread-safe; atomics only.
     */
    ExpectedVoid pause() {
        auto s = playbackState_.load(std::memory_order_acquire);
        if (s != PlaybackState::Playing)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "not playing"));
        // record pause position
        pausePos_ = currentPositionLocked();
        playbackState_.store(PlaybackState::Paused, std::memory_order_release);
        if (output_)
            output_->stop();
        return {};
    }

    /**
     * @brief Resumes from paused.
     * @ingroup caudio_engine
     * @return Success or Error State if not paused.
     * @par Thread safety
     * Thread-safe; notifies decode/monitor cvs.
     */
    ExpectedVoid resume() {
        auto s = playbackState_.load(std::memory_order_acquire);
        if (s != PlaybackState::Paused)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "not paused"));
        playStart_ = std::chrono::steady_clock::now();
        playbackState_.store(PlaybackState::Playing, std::memory_order_release);
        if (output_)
            output_->start();
        decodeCv_.notify_all();
        monCv_.notify_all();
        return {};
    }

    /**
     * @brief Stops playback and resets position/ring.
     * @ingroup caudio_engine
     * @return Success (always).
     * @par Thread safety
     * Thread-safe.
     * @see play
     */
    ExpectedVoid stop() {
        playbackState_.store(PlaybackState::Stopped, std::memory_order_release);
        if (output_)
            output_->stop();
        if (ring_)
            ring_->reset();
        pausePos_ = 0;
        hasCurrent_.store(false, std::memory_order_release);
        return {};
    }

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
    ExpectedVoid seek(double seconds) {
        if (!hasCurrent_.load(std::memory_order_acquire))
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no track"));
        if (!std::isfinite(seconds) || seconds < 0)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "bad seconds"));

        // Pause decode thread to safely seek + reset ring (prevents race with decodeLoop)
        std::unique_lock<std::mutex> lk(decodeMtx_);
        playbackState_.store(PlaybackState::Paused, std::memory_order_release);
        // snapshot for restore on error (Task 1: seek hardening)
        double savedPause = pausePos_;
        auto savedStart = playStart_;

        if (decoder_) {
            auto res = decoder_->seek(seconds);
            if (!res) {
                pausePos_ = savedPause;
                playStart_ = savedStart;
                playbackState_.store(PlaybackState::Playing, std::memory_order_release);
                return std::unexpected(res.error());
            }
        }

        // adjust position tracking
        double dur = duration_;
        if (seconds > dur)
            seconds = dur;
        pausePos_ = seconds;
        playStart_ = std::chrono::steady_clock::now();
        if (ring_)
            ring_->reset();

        // Resume decode thread
        playbackState_.store(PlaybackState::Playing, std::memory_order_release);
        decodeCv_.notify_all();
        return {};
    }

    /**
     * @brief Clamps volume to [0,1], mapping non-finite to 0.
     * @ingroup caudio_engine
     * @param v Input volume.
     * @return Clamped value.
     */
    static inline float clampVolume(float v) noexcept {
        if (!std::isfinite(v))
            return 0.0f;
        return std::clamp(v, 0.0f, 1.0f);
    }

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
    ExpectedVoid setVolume(float g) {
        g = clampVolume(g);
        state_.volume = g;
        volume_.store(g, std::memory_order_relaxed);
        if (output_)
            output_->setVolume(g);
        if (hasDb()) {
            state_.cursorPos = (int64_t)queue_.cursor;
            (void)saveState();
        }
        return {};
    }

    /** @brief Returns current volume [0,1]. @ingroup caudio_engine
     * @return Volume level (atomic load, relaxed). */
    float volume() const noexcept {
        return volume_.load(std::memory_order_relaxed);
    }
    /** @brief Returns current playback state. @ingroup caudio_engine
     * @return PlaybackState (atomic load, acquire). */
    PlaybackState state() const noexcept {
        return playbackState_.load(std::memory_order_acquire);
    }
    /** @brief Returns current track duration in seconds. @ingroup caudio_engine
     * @return Duration (cached from decoder or metadata). */
    double duration() const noexcept {
        return duration_;
    }
    /** @brief Returns current playback position in seconds. @ingroup caudio_engine
     * @return Position (0 if no track; pausePos_ if paused; pausePos_ + elapsed if playing, clamped). */
    double position() const noexcept {
        return currentPositionLocked();
    }
    /** @brief Returns current track id (or persisted one if no current). @ingroup caudio_engine
     * @return Track id if hasCurrent_, else state_.currentTrackId (persisted). */
    int64_t currentTrackId() const noexcept {
        if (hasCurrent_.load(std::memory_order_acquire))
            return currentTrack_.id;
        return state_.currentTrackId;
    }

    /** @brief Returns whether shuffle is enabled. @ingroup caudio_engine
     * @return True if shuffle mode active. */
    bool shuffle() const noexcept {
        return queue_.shuffle;
    }

    /** @brief Returns current repeat mode. @ingroup caudio_engine
     * @return RepeatMode (Off/Queue/One). */
    RepeatMode repeat() const noexcept {
        return queue_.repeat;
    }

    /**
     * @brief Returns library version (full git tag).
     * @ingroup caudio_engine
     * @return Version string (kVersionFull, e.g. "v0.25.4").
     * @details Additive, no API break. Delegates to caudio::utils::kVersionFull via imported version partition.
     */
    std::string_view version() const noexcept {
        return caudio::utils::kVersionFull;
    }

    /**
     * @brief Returns library version (static).
     * @ingroup caudio_engine
     * @return Version string (kVersionFull).
     */
    static constexpr std::string_view staticVersion() noexcept {
        return caudio::utils::kVersionFull;
    }

    /**
     * @brief Returns database statistics.
     * @ingroup caudio_engine
     * @return `std::expected<DbStats, Error>` — stats on success, State if no db.
     * @par Thread safety
     * Thread-safe; delegates to Database::getStats() which takes shared_lock.
     * @see caudio::db::Database::getStats
     * @see caudio::db::DbStats
     */
    std::expected<caudio::db::DbStats, caudio::utils::Error> getStats() {
        if (!hasDb())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
        return db_->getStats();
    }

    /**
     * @brief Lists history entries.
     * @ingroup caudio_engine
     * @param limit Max rows (0 = no limit, default 50).
     * @return `std::expected<std::vector<HistoryEntry>, Error>` — vector on success, State if no db.
     * @par Thread safety
     * Thread-safe; History::listHistory takes shared_lock on its mutex.
     * @see History::listHistory
     * @see HistoryEntry
     */
    std::expected<std::vector<HistoryEntry>, caudio::utils::Error> listHistory(int limit = 50) {
        if (!hasDb())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
        caudio::engine::History hist(db_);
        return hist.listHistory(limit);
    }

    /**
     * @brief Clears all history entries.
     * @ingroup caudio_engine
     * @return `std::expected<void, Error>` — success or State/Internal.
     * @par Thread safety
     * Thread-safe; History::clearHistory takes unique_lock on its mutex.
     * @see History::clearHistory
     */
    std::expected<void, caudio::utils::Error> clearHistory() {
        if (!hasDb())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
        caudio::engine::History hist(db_);
        return hist.clearHistory();
    }

    /**
     * @brief Returns last decoder/output error string.
     * @ingroup caudio_engine
     * @return Error message or empty string if none.
     * @par Thread safety
     * Thread-safe; reads lastErr_ (written from doPlayTrack, not atomic but single-writer).
     * @see doPlayTrack
     */
    std::string lastError() const {
        if (!lastErr_.empty())
            return lastErr_;
        return "";
    }

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
    ExpectedVoid setShuffle(bool on) {
        if (!hasDb())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
        if (!tryLockQueue())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Busy, "busy"));
        auto r = setShuffleLocked(on);
        unlockQueue();
        if (!r)
            return std::unexpected(r.error());
        // push queue changed event
        EngineEvent ev;
        ev.type = EngineEventType::QueueChanged;
        ev.queue_id = queue_.queue_id;
        pushEvent(ev);
        return {};
    }

    /**
     * @brief Sets repeat mode and persists it.
     * @ingroup caudio_engine
     * @param m RepeatMode (Off/Queue/One).
     * @return Success or Error InvalidArg/Busy/Internal.
     * @retval StatusCode::InvalidArg if m is out of range.
     * @details Under queueMutex_: updates queue_.repeat/state_.repeatMode
     * and persists via saveState() (handles active_queue_id migration).
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_.
     */
    ExpectedVoid setRepeat(RepeatMode m) {
        if (m != RepeatMode::Off && m != RepeatMode::Queue && m != RepeatMode::One)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "bad repeat"));
        if (!tryLockQueue())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Busy, "busy"));
        queue_.repeat = m;
        state_.repeatMode = m;
        state_.cursorPos = (int64_t)queue_.cursor;
        std::optional<caudio::utils::Error> err;
        if (auto e = saveState(); !e)
            err = e.error();
        unlockQueue();
        if (err)
            return std::unexpected(*err);
        return {};
    }

    /**
     * @brief Returns the active queue id.
     * @ingroup caudio_engine
     * @return queue_.queue_id if set, else state_.activeQueueId, else 1 (atomic under queueMutex_).
     * @par Thread safety
     * Thread-safe; locks queueMutex_.
     * @see switchQueue
     */
    int64_t activeQueueId() const noexcept {
        std::lock_guard<std::mutex> lk(queueMutex_);
        return queue_.queue_id ? queue_.queue_id : state_.activeQueueId ? state_.activeQueueId : 1;
    }

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
    ExpectedVoid switchQueue(int64_t qid) {
        if (qid == 0)
            qid = 1;
        if (!hasDb())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
        if (!tryLockQueue())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Busy, "busy"));
        // validate queue exists (holds queueMutex_ -> dbMutex_ shared)
        {
            auto q = db_->getQueue(qid);
            if (!q) {
                unlockQueue();
                return std::unexpected(q.error());
            }
        }
        if (queue_.queue_id == qid) {
            // already active, ensure cursor consistent with DB state but not error
            unlockQueue();
            return {};
        }
        // switch active queue: reset cursor, clear shuffle perm, update state
        queue_.queue_id = qid;
        queue_.cursor = 0;
        queue_.perm.clear();
        state_.activeQueueId = qid;
        state_.cursorPos = 0;
        // keep shuffle flag as-is but perm cleared; next shuffle will regenerate for new queue
        std::optional<caudio::utils::Error> err;
        if (auto e = saveState(); !e)
            err = e.error();
        // also persist via saveState includes active_queue_id, cursor_pos, shuffle_perm cleared
        unlockQueue();
        if (err)
            return std::unexpected(*err);
        EngineEvent ev;
        ev.type = EngineEventType::QueueChanged;
        ev.queue_id = qid;
        pushEvent(ev);
        return {};
    }

    /**
     * @brief Advances to the next track per RepeatMode/shuffle.
     * @ingroup caudio_engine
     * @return Success or Error State/Busy/NotFound.
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
    ExpectedVoid next() {
        if (!hasDb())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
        // handle repeat one without shuffle without queue lock? match C: if !shuffle && repeat==One
        // && hasCurrent => seek 0 and play - serialize with decodeLoop via decodeMtx_
        if (!queue_.shuffle && queue_.repeat == RepeatMode::One &&
            hasCurrent_.load(std::memory_order_acquire)) {
            {
                std::unique_lock<std::mutex> lk(decodeMtx_);
                if (decoder_) {
                    auto r = decoder_->seek(0);
                    if (!r)
                        return std::unexpected(r.error());
                }
                if (ring_)
                    ring_->reset();
            }
            pausePos_ = 0;
            playStart_ = std::chrono::steady_clock::now();
            playbackState_.store(PlaybackState::Playing, std::memory_order_release);
            markedPlayed_.store(false, std::memory_order_release);
            gaplessArmed_.store(false, std::memory_order_release);
            startedMs_ = (int64_t)detail::nowMs();
            EngineEvent ev;
            ev.type = EngineEventType::TrackStarted;
            ev.track_id = currentTrack_.id;
            ev.queue_id = queue_.queue_id;
            ev.duration = duration_;
            pushEvent(ev);
            decodeCv_.notify_all();
            return {};
        }
        if (!tryLockQueue())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Busy, "busy"));
        caudio::db::Track t;
        auto r = queueNextLocked(t);
        if (!r) {
            unlockQueue();
            return std::unexpected(r.error());
        }
        auto pr = doPlayTrack(t);
        unlockQueue();
        if (!pr)
            return std::unexpected(pr.error());
        return {};
    }

    /**
     * @brief Moves to the previous track.
     * @ingroup caudio_engine
     * @return Success or Error State/Busy/NotFound ("at start"/"no perm").
     * @details Locks queueMutex_ try_lock then queuePrevLocked + doPlayTrack.
     * Shuffle path steps cursor back by 2 and clamps; non-shuffle mirrors.
     * @par Thread safety
     * Thread-safe; try_lock on queueMutex_.
     * @see queuePrevLocked
     */
    ExpectedVoid prev() {
        if (!hasDb())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
        if (!tryLockQueue())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Busy, "busy"));
        caudio::db::Track t;
        auto r = queuePrevLocked(t);
        if (!r) {
            unlockQueue();
            return std::unexpected(r.error());
        }
        auto pr = doPlayTrack(t);
        unlockQueue();
        if (!pr)
            return std::unexpected(pr.error());
        return {};
    }

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
    ExpectedVoid setCallbacks(const EngineCallbacks& cbs) {
        std::lock_guard<std::mutex> lk(cbMutex_);
        callbacks_ = cbs;
        cfg_.callbacks = cbs;
        return {};
    }

    /**
     * @brief Pops one event from the MPSC queue (non-blocking).
     * @ingroup caudio_engine
     * @return `std::expected<EngineEvent, Error>` — event on success, NotFound if queue empty.
     * @par Thread safety
     * Thread-safe; MpscQueue is lock-free MPSC.
     * @see drainEvents
     * @see drainAll
     * @see EngineEvent
     */
    std::expected<EngineEvent, caudio::utils::Error> pollEvent() {
        auto r = eventQueue_.pop();
        if (!r)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "no event"));
        return r.value();
    }

    /**
     * @brief Drains up to cap events into a caller-provided buffer.
     * @ingroup caudio_engine
     * @param buf Output buffer (may be null if cap==0).
     * @param cap Capacity of buf.
     * @param n Out: number of events written (must be non-null).
     * @return `std::expected<void, Error>` — success or InvalidArg if n is null or buf is null with cap>0.
     * @par Thread safety
     * Thread-safe; pops from MpscQueue (lock-free).
     * @see pollEvent
     * @see drainAll
     * @see EngineEvent
     */
    ExpectedVoid drainEvents(EngineEvent* buf, size_t cap, size_t* n) {
        if (!n)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "null n"));
        *n = 0;
        if (!buf && cap != 0)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "null buf"));
        size_t got = 0;
        while (got < cap) {
            auto r = eventQueue_.pop();
            if (!r)
                break;
            buf[got++] = r.value();
        }
        *n = got;
        return {};
    }

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
    std::vector<EngineEvent> drainAll() {
        std::vector<EngineEvent> out;
        while (true) {
            auto r = eventQueue_.pop();
            if (!r)
                break;
            out.push_back(r.value());
        }
        return out;
    }

    /**
     * @brief Compatibility alias for state().
     * @ingroup caudio_engine
     * @return Current PlaybackState (Stopped/Playing/Paused).
     * @see state()
     */
    // compat: use state()/position()
    PlaybackState getState() const noexcept {
        return state();
    }
    /**
     * @brief Compatibility alias for position().
     * @ingroup caudio_engine
     * @return Current playback position in seconds.
     * @see position()
     */
    // compat: use state()/position()
    double getPosition() const noexcept {
        return position();
    }

  private:
    /**
     * @brief Constructs Engine with config defaults applied.
     * @ingroup caudio_engine
     * @param cfg EngineConfig; zero poll/gapless/history values are replaced by defaults.
     * @details Applies defaults: pollMs 10, gaplessMs 300, historyThresholdPct 60,
     * historyThresholdSecs 90; initializes atomics and copies callbacks.
     */
    explicit Engine(const EngineConfig& cfg) : cfg_(cfg), state_{}, queue_{} {
        cfg_.pollMs = cfg.pollMs ? cfg.pollMs : 10;
        cfg_.gaplessMs = cfg.gaplessMs ? cfg.gaplessMs : 300;
        cfg_.historyThresholdPct = cfg.historyThresholdPct ? cfg.historyThresholdPct : 60;
        cfg_.historyThresholdSecs = cfg.historyThresholdSecs ? cfg.historyThresholdSecs : 90;
        state_.volume = 1.0f;
        volume_.store(1.0f, std::memory_order_relaxed);
        state_.repeatMode = RepeatMode::Off;
        queue_.repeat = RepeatMode::Off;
        queue_.queue_id = 1;
        playbackState_.store(PlaybackState::Stopped, std::memory_order_release);
        hasCurrent_.store(false, std::memory_order_release);
        markedPlayed_.store(false, std::memory_order_release);
        gaplessArmed_.store(false, std::memory_order_release);
        lastProgressMs_.store(0, std::memory_order_release);
        // callbacks from config
        callbacks_ = cfg.callbacks;
    }

    /**
     * @brief Starts decode and (optionally) monitor threads.
     * @ingroup caudio_engine
     * @return std::nullopt on success.
     * @details Sets decodeRun_/monRun_, spawns jthreads running
     * decodeLoop/monitorLoop. Called from create()/open().
     * @par Thread safety
     * Called during construction before shared access.
     */
    std::optional<caudio::utils::Error> init() {
        // start decode thread
        decodeRun_.store(true, std::memory_order_release);
        decodeThread_ = std::jthread([this](std::stop_token st) { decodeLoop(st); });
        // start monitor thread if enabled
        if (cfg_.enableMonitorThread) {
            monRun_.store(true, std::memory_order_release);
            monitorThread_ = std::jthread([this](std::stop_token st) { monitorLoop(st); });
        }
        return std::nullopt;
    }

    /** @brief Returns true if DB handle is attached. @ingroup caudio_engine */
    bool hasDb() const noexcept {
        return db_ && db_->handle();
    }

    /**
     * @brief Tries to acquire queueMutex_ without blocking.
     * @ingroup caudio_engine
     * @return true if lock acquired; false means Busy.
     */
    bool tryLockQueue() noexcept {
        return queueMutex_.try_lock();
    }
    /** @brief Releases queueMutex_. @ingroup caudio_engine */
    void unlockQueue() noexcept {
        queueMutex_.unlock();
    }

    /**
     * @brief Computes current playback position in seconds.
     * @ingroup caudio_engine
     * @return Position; 0 if no current track; pausePos_ if Paused/Stopped,
     * otherwise pausePos_ + elapsed since playStart_ clamped to duration.
     * @par Thread safety
     * Lock-free; reads atomics.
     */
    double currentPositionLocked() const noexcept {
        if (!hasCurrent_.load(std::memory_order_acquire))
            return 0.0;
        auto st = playbackState_.load(std::memory_order_acquire);
        if (st == PlaybackState::Paused)
            return pausePos_;
        if (st != PlaybackState::Playing)
            return pausePos_;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - playStart_).count();
        double pos = pausePos_ + elapsed;
        if (pos > duration_)
            pos = duration_;
        if (pos < 0)
            pos = 0;
        return pos;
    }

    // State persistence
    /**
     * @brief Returns borrowed sqlite3* handle or nullptr if no database.
     * @ingroup caudio_engine
     * @return Raw sqlite3 pointer (owned by Database) or nullptr.
     * @par Thread safety
     * Thread-safe; reads shared_ptr atomically.
     */
    sqlite3* dbHandle() const noexcept {
        if (db_)
            return db_->handle();
        return nullptr;
    }
    /**
     * @brief Returns pointer to Database mutex or nullptr if no database.
     * @ingroup caudio_engine
     * @return Pointer to shared_mutex (owned by Database) or nullptr.
     * @par Thread safety
     * Thread-safe; reads shared_ptr atomically.
     */
    std::shared_mutex* dbMutex() const noexcept {
        if (db_)
            return &db_->mutex();
        return nullptr;
    }

    /**
     * @brief Executes a callable inside a SQLite BEGIN IMMEDIATE / COMMIT transaction.
     * @ingroup caudio_engine
     * @tparam Fn Callable with signature `std::expected<void, caudio::utils::Error>(sqlite3*)`.
     * @param fn Transaction body; receives the database handle.
     * @return `std::expected<void, Error>` — success or error (Busy if begin fails, Internal on commit failure, InvalidArg if no db).
     * @details Acquires unique_lock on dbMutex_, executes `BEGIN IMMEDIATE`, runs `fn(h)`,
     * commits on success, rolls back on any failure. Used by all state persistence methods.
     * @par Thread safety
     * Locks dbMutex_ exclusively; callers must not hold queueMutex_ to avoid deadlock.
     */
    template <typename Fn>
    std::expected<void, caudio::utils::Error> withTransaction(Fn&& fn) {
        auto* h = dbHandle();
        auto* m = dbMutex();
        if (!h || !m)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "no db"));
        std::unique_lock<std::shared_mutex> lk(*m);
        char* err = nullptr;
        int rc = sqlite3_exec(h, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        SqliteErrGuard errGuard{err};
        if (rc != SQLITE_OK)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Busy, "begin failed"));

        auto result = fn(h);
        if (!result) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            return result;
        }
        char* commitErr = nullptr;
        rc = sqlite3_exec(h, "COMMIT", nullptr, nullptr, &commitErr);
        SqliteErrGuard commitGuard{commitErr};
        if (rc != SQLITE_OK) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "commit failed"));
        }
        return {};
    }

    /**
     * @brief Loads persisted engine state from engine_state row id=1.
     * @ingroup caudio_engine
     * @return `std::optional<Error>` — `std::nullopt` on success (including no row, defaults applied), or Error on failure.
     * @details Tries `sqlNew` (with `active_queue_id` column) first; if prepare fails (old schema),
     * falls back to `sqlOld` without that column for migration. On row found:
     * - Restores EngineState fields: shuffle_enabled, repeat_mode, cursor_pos, current_track_id, volume, active_queue_id.
     * - Deserializes `shuffle_perm` BLOB into `queue_.perm` if shuffleEnabled.
     * - Validates `active_queue_id` (>=1, checks queues table) and clamps cursor to permutation size.
     * On SQLITE_DONE (no row): resets all state to defaults (shuffle=off, repeat=Off, cursor=0, volume=1.0, queue_id=1).
     * Holds dbMutex_ unique_lock throughout.
     * @par Thread safety
     * Locks dbMutex_ exclusively; callers may hold queueMutex_ externally but not required.
     * @see saveState
     * @see persistShuffleBlobLocked
     * @see persistCursorLocked
     */
    std::optional<caudio::utils::Error> loadState() {
        auto* h = dbHandle();
        auto* m = dbMutex();
        if (!h || !m)
            return caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "no db");
        std::unique_lock<std::shared_mutex> lk(*m);
        // Try new schema with active_queue_id; fallback to old schema for migration
        const char* sqlNew = "SELECT shuffle_enabled, repeat_mode, cursor_pos, current_track_id, "
                             "volume, shuffle_perm, active_queue_id FROM engine_state WHERE id=1";
        const char* sqlOld = "SELECT shuffle_enabled, repeat_mode, cursor_pos, current_track_id, "
                             "volume, shuffle_perm FROM engine_state WHERE id=1";
        sqlite3_stmt* raw = nullptr;
        int rc = sqlite3_prepare_v2(h, sqlNew, -1, &raw, nullptr);
        bool hasActiveCol = true;
        if (rc != SQLITE_OK) {
            // fallback to old schema without active_queue_id
            sqlite3_finalize(raw);
            raw = nullptr;
            rc = sqlite3_prepare_v2(h, sqlOld, -1, &raw, nullptr);
            hasActiveCol = false;
            if (rc != SQLITE_OK) {
                return caudio::utils::makeError(caudio::utils::StatusCode::Internal, "prepare failed");
            }
        }
        StmtGuard stmt(raw);
        raw = stmt.get();
        rc = sqlite3_step(raw);
        if (rc == SQLITE_ROW) {
            state_.shuffleEnabled = sqlite3_column_int(raw, 0);
            state_.repeatMode = (RepeatMode)sqlite3_column_int(raw, 1);
            state_.cursorPos = sqlite3_column_int64(raw, 2);
            state_.currentTrackId = sqlite3_column_int64(raw, 3);
            state_.volume = (float)sqlite3_column_double(raw, 4);
            if (state_.volume < 0 || state_.volume > 1)
                state_.volume = 1.0f;
            volume_.store(state_.volume, std::memory_order_relaxed);
            const void* blob = sqlite3_column_blob(raw, 5);
            int blobBytes = sqlite3_column_bytes(raw, 5);
            if (hasActiveCol) {
                state_.activeQueueId = sqlite3_column_int64(raw, 6);
                if (state_.activeQueueId <= 0)
                    state_.activeQueueId = 1;
            } else {
                state_.activeQueueId = 1;
            }
            queue_.perm.clear();
            queue_.perm.shrink_to_fit();
            queue_.cursor = 0;
            if (blob && blobBytes > 0 && state_.shuffleEnabled) {
                size_t n = (size_t)blobBytes / sizeof(int64_t);
                queue_.perm.resize(n);
                std::memcpy(queue_.perm.data(), blob, n * sizeof(int64_t));
            } else {
                queue_.perm.clear();
            }
            queue_.shuffle = state_.shuffleEnabled ? true : false;
            queue_.repeat = state_.repeatMode;
            if (state_.cursorPos < 0)
                queue_.cursor = 0;
            else {
                queue_.cursor = (size_t)state_.cursorPos;
                if (!queue_.perm.empty() && queue_.cursor >= queue_.perm.size())
                    queue_.cursor = 0;
                // for non-shuffle, keep cursor as stored (persistent queue via cursor)
                // clamp later via queueCount if needed; don't reset to 0
            }
            queue_.queue_id = state_.activeQueueId;
            // validate queue exists; fallback to 1 if not
            {
                sqlite3_stmt* chk = nullptr;
                if (sqlite3_prepare_v2(h, "SELECT id FROM queues WHERE id=?", -1, &chk, nullptr) ==
                    SQLITE_OK) {
                    sqlite3_bind_int64(chk, 1, queue_.queue_id);
                    int step = sqlite3_step(chk);
                    if (step != SQLITE_ROW) {
                        queue_.queue_id = 1;
                        state_.activeQueueId = 1;
                    }
                    sqlite3_finalize(chk);
                }
            }
            return std::nullopt;
        }
        if (rc == SQLITE_DONE) {
            state_.shuffleEnabled = 0;
            state_.repeatMode = RepeatMode::Off;
            state_.cursorPos = 0;
            state_.currentTrackId = 0;
            state_.volume = 1.0f;
            state_.activeQueueId = 1;
            volume_.store(1.0f, std::memory_order_relaxed);
            queue_.perm.clear();
            queue_.cursor = 0;
            queue_.shuffle = false;
            queue_.repeat = RepeatMode::Off;
            queue_.queue_id = 1;
            return std::nullopt;
        }
        return caudio::utils::makeError(caudio::utils::StatusCode::Internal, "load failed");
    }

    /**
     * @brief Persists current engine state to engine_state row id=1.
     * @ingroup caudio_engine
     * @return `std::expected<void, Error>` — success or error (Internal on prepare/step failure, NoMem if perm too large).
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
    std::expected<void, caudio::utils::Error> saveState() {
        return withTransaction([&](sqlite3* db) -> std::expected<void, caudio::utils::Error> {
            // Try with active_queue_id column; fallback to old schema if missing
            const char* sqlNew =
                "UPDATE engine_state SET shuffle_enabled=?, repeat_mode=?, shuffle_perm=?, "
                "cursor_pos=?, current_track_id=?, volume=?, active_queue_id=?, "
                "updated=CURRENT_TIMESTAMP WHERE id=1";
            const char* sqlOld =
                "UPDATE engine_state SET shuffle_enabled=?, repeat_mode=?, shuffle_perm=?, "
                "cursor_pos=?, current_track_id=?, volume=?, updated=CURRENT_TIMESTAMP WHERE id=1";
            sqlite3_stmt* raw = nullptr;
            int rc = sqlite3_prepare_v2(db, sqlNew, -1, &raw, nullptr);
            bool hasActiveCol = true;
            if (rc != SQLITE_OK) {
                sqlite3_finalize(raw);
                raw = nullptr;
                rc = sqlite3_prepare_v2(db, sqlOld, -1, &raw, nullptr);
                hasActiveCol = false;
                if (rc != SQLITE_OK)
                    return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                                    "prepare failed"));
            }
            StmtGuard stmt(raw);
            raw = stmt.get();
            sqlite3_bind_int(raw, 1, state_.shuffleEnabled);
            sqlite3_bind_int(raw, 2, (int)state_.repeatMode);
            if (queue_.shuffle && !queue_.perm.empty()) {
                if (queue_.perm.size() >
                    (size_t)(std::numeric_limits<int>::max() / (int)sizeof(int64_t))) {
                    return std::unexpected(caudio::utils::makeError(
                        caudio::utils::StatusCode::NoMem, "perm too large"));
                }
                sqlite3_bind_blob(raw, 3, queue_.perm.data(),
                                   (int)(queue_.perm.size() * sizeof(int64_t)), SQLITE_TRANSIENT);
            } else
                sqlite3_bind_null(raw, 3);
            sqlite3_bind_int64(raw, 4, state_.cursorPos);
            sqlite3_bind_int64(raw, 5, state_.currentTrackId);
            sqlite3_bind_double(raw, 6, (double)state_.volume);
            if (hasActiveCol) {
                sqlite3_bind_int64(raw, 7, state_.activeQueueId ? state_.activeQueueId : queue_.queue_id);
            }
            rc = sqlite3_step(raw);
            if (rc != SQLITE_DONE)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::Internal, "step failed"));
            return {};
        });
    }

    /**
     * @brief Persists shuffle permutation and cursor position atomically.
     * @ingroup caudio_engine
     * @return `std::expected<void, Error>` — success or error (Internal on prepare/step failure, NoMem if perm too large).
     * @details Executes `UPDATE engine_state SET shuffle_perm=?, cursor_pos=?, shuffle_enabled=? WHERE id=1`
     * inside a transaction. Binds the current `queue_.perm` as BLOB (or NULL if not shuffling),
     * `queue_.cursor`, and `queue_.shuffle`. Also updates `state_.shuffleEnabled` and `state_.cursorPos`.
     * Called by `setShuffleLocked`, `queueNextLocked`, `queuePrevLocked` after modifying shuffle/cursor.
     * @par Thread safety
     * Caller must hold `queueMutex_`; locks dbMutex_ exclusively via withTransaction().
     * @see setShuffleLocked
     * @see queueNextLocked
     * @see queuePrevLocked
     * @see withTransaction
     */
    std::expected<void, caudio::utils::Error> persistShuffleBlobLocked() {
        return withTransaction([&](sqlite3* db) -> std::expected<void, caudio::utils::Error> {
            const char* sql = "UPDATE engine_state SET shuffle_perm=?, cursor_pos=?, "
                              "shuffle_enabled=? WHERE id=1";
            sqlite3_stmt* raw = nullptr;
            int rc = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
            StmtGuard stmt(raw);
            if (rc != SQLITE_OK)
                return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                                "prepare failed"));
            raw = stmt.get();
            if (queue_.shuffle && !queue_.perm.empty()) {
                if (queue_.perm.size() >
                    (size_t)(std::numeric_limits<int>::max() / (int)sizeof(int64_t))) {
                    return std::unexpected(
                        caudio::utils::makeError(caudio::utils::StatusCode::NoMem, "perm large"));
                }
                sqlite3_bind_blob(raw, 1, queue_.perm.data(),
                                  (int)(queue_.perm.size() * sizeof(int64_t)), SQLITE_TRANSIENT);
            } else
                sqlite3_bind_null(raw, 1);
            sqlite3_bind_int64(raw, 2, (int64_t)queue_.cursor);
            sqlite3_bind_int(raw, 3, queue_.shuffle ? 1 : 0);
            rc = sqlite3_step(raw);
            if (rc != SQLITE_DONE)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::Internal, "step failed"));
            // also update state
            state_.shuffleEnabled = queue_.shuffle ? 1 : 0;
            state_.cursorPos = (int64_t)queue_.cursor;
            return {};
        });
    }

    /**
     * @brief Persists only the cursor position to engine_state.
     * @ingroup caudio_engine
     * @return `std::expected<void, Error>` — success or error (Internal on prepare/step failure).
     * @details Executes `UPDATE engine_state SET cursor_pos=? WHERE id=1` inside a transaction.
     * Binds `queue_.cursor`. Also updates `state_.cursorPos`. Lighter than persistShuffleBlobLocked;
     * used when only cursor advances (non-shuffle next/prev).
     * @par Thread safety
     * Caller must hold `queueMutex_`; locks dbMutex_ exclusively via withTransaction().
     * @see persistShuffleBlobLocked
     * @see withTransaction
     */
    std::expected<void, caudio::utils::Error> persistCursorLocked() {
        return withTransaction([&](sqlite3* db) -> std::expected<void, caudio::utils::Error> {
            const char* sql = "UPDATE engine_state SET cursor_pos=? WHERE id=1";
            sqlite3_stmt* raw = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
                return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                                "prepare failed"));
            StmtGuard stmt(raw);
            raw = stmt.get();
            sqlite3_bind_int64(raw, 1, (int64_t)queue_.cursor);
            int rc = sqlite3_step(raw);
            if (rc != SQLITE_DONE)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::Internal, "step failed"));
            state_.cursorPos = (int64_t)queue_.cursor;
            return {};
        });
    }

/**
     * @brief Fetches track from queue at given position (under db shared_lock).
     * @ingroup caudio_engine
     * @param qid Queue id (0 defaults to 1).
     * @param pos Zero-based position in queue.
     * @return `std::expected<Track, Error>` — track on success, NotFound if position out of range, Internal on SQL error.
     * @details Executes `SELECT track_id FROM queue WHERE queue_id=? ORDER BY position LIMIT 1 OFFSET ?`
     * under dbMutex_ shared_lock, then calls `db_->getTrack(trackId)`.
     * @par Thread safety
     * Locks dbMutex_ shared; caller typically holds queueMutex_.
     * @see queueNextLocked
     * @see queuePrevLocked
     */
    std::expected<caudio::db::Track, caudio::utils::Error> fetchTrackByPosLocked(int64_t qid,
                                                                                 int64_t pos) {
        if (qid == 0)
            qid = 1;
        auto* h = dbHandle();
        auto* m = dbMutex();
        if (!h || !m)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "no db"));
        int64_t trackId = 0;
        {
            std::shared_lock<std::shared_mutex> lk(*m);
            const char* sql =
                "SELECT track_id FROM queue WHERE queue_id=? ORDER BY position LIMIT 1 OFFSET ?";
            sqlite3_stmt* raw = nullptr;
            if (sqlite3_prepare_v2(h, sql, -1, &raw, nullptr) != SQLITE_OK)
                return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                                "prepare failed"));
            StmtGuard stmt(raw);
            raw = stmt.get();
            sqlite3_bind_int64(raw, 1, qid);
            sqlite3_bind_int64(raw, 2, pos);
            int rc = sqlite3_step(raw);
            if (rc != SQLITE_ROW) {
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "not found"));
            }
            trackId = sqlite3_column_int64(raw, 0);
        }
        auto tr = db_->getTrack(trackId);
        if (!tr)
            return std::unexpected(tr.error());
        return tr.value();
    }

    /**
     * @brief Internal shuffle toggle with persistence (queueMutex_ must be held).
     * @ingroup caudio_engine
     * @param on True to enable shuffle, false to disable.
     * @return `std::expected<void, Error>` — success or error from persistShuffleBlobLocked.
     * @details If enabling and perm empty: clears perm/cursor, gets queue count, generates new
     * permutation via `detail::shufflePerm` with random_device, persists via persistShuffleBlobLocked.
     * If disabling: clears perm, sets shuffle=false, persists. No-op if state unchanged and perm non-empty.
     * Updates `state_.shuffleEnabled` on success.
     * @par Thread safety
     * Caller MUST hold `queueMutex_` (acquired via tryLockQueue()). Locks dbMutex_ via persistShuffleBlobLocked.
     * @see setShuffle
     * @see persistShuffleBlobLocked
     * @see detail::shufflePerm
     */
    std::expected<void, caudio::utils::Error> setShuffleLocked(bool on) {
        // assert: queueMutex_ locked by caller
        bool want = on;
        if (queue_.shuffle == want && !queue_.perm.empty())
            return {};
        if (want) {
            queue_.perm.clear();
            queue_.cursor = 0;
            size_t cnt = db_->queueCountLocked(queue_.queue_id);
            if (cnt == 0) {
                queue_.shuffle = true;
                queue_.perm.clear();
                queue_.cursor = 0;
                auto r = persistShuffleBlobLocked();
                if (!r)
                    return r;
                state_.shuffleEnabled = 1;
                return {};
            }
            queue_.perm.resize(cnt);
            for (size_t i = 0; i < cnt; ++i)
                queue_.perm[i] = (int64_t)i;
            {
                std::mt19937 rng{std::random_device{}()};
                detail::shufflePerm(queue_.perm, rng);
            }
            queue_.shuffle = true;
            queue_.cursor = 0;
            auto r = persistShuffleBlobLocked();
            if (!r)
                return r;
            state_.shuffleEnabled = 1;
            return {};
        } else {
            queue_.perm.clear();
            queue_.cursor = 0;
            queue_.shuffle = false;
            auto r = persistShuffleBlobLocked();
            if (!r)
                return r;
            state_.shuffleEnabled = 0;
            return {};
        }
    }

    /**
     * @brief Advances queue cursor to next track per shuffle/repeat mode (queueMutex_ held).
     * @ingroup caudio_engine
     * @param out Output track reference (filled on success).
     * @return `std::expected<void, Error>` — success, NotFound if queue empty, Internal on fetch error.
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
     * Caller MUST hold `queueMutex_` (acquired via tryLockQueue()). Locks dbMutex_ shared for fetch,
     * exclusive via persistCursorLocked/persistShuffleBlobLocked.
     * @see next
     * @see queuePrevLocked
     * @see setShuffleLocked
     * @see persistCursorLocked
     * @see persistShuffleBlobLocked
     */
    std::expected<void, caudio::utils::Error> queueNextLocked(caudio::db::Track& out) {
        if (queue_.queue_id == 0)
            queue_.queue_id = 1;
        if (queue_.shuffle) {
            if (queue_.perm.empty()) {
                size_t cnt = db_->queueCountLocked(queue_.queue_id);
                if (cnt == 0)
                    return std::unexpected(caudio::utils::makeError(
                        caudio::utils::StatusCode::NotFound, "empty queue"));
                auto sr = setShuffleLocked(true);
                if (!sr)
                    return std::unexpected(sr.error());
                if (queue_.perm.empty())
                    return std::unexpected(
                        caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no perm"));
            }
            if (queue_.cursor >= queue_.perm.size()) {
                if (queue_.repeat == RepeatMode::One) {
                    size_t idx = queue_.perm.size() - 1;
                    if (queue_.cursor > 0 && queue_.cursor <= queue_.perm.size())
                        idx = queue_.cursor - 1;
                    int64_t pos = queue_.perm[idx];
                    auto tr = fetchTrackByPosLocked(queue_.queue_id, pos);
                    if (!tr)
                        return std::unexpected(tr.error());
                    out = tr.value();
                    return {};
                } else {
                    // wrap/reshuffle for both Off and Queue (shuffle on => new perm)
                    auto sr = setShuffleLocked(true);
                    if (!sr)
                        return std::unexpected(sr.error());
                    queue_.cursor = 0;
                    (void)persistCursorLocked();
                }
            }
            int64_t pos = queue_.perm[queue_.cursor];
            queue_.cursor++;
            (void)persistCursorLocked();
            auto tr = fetchTrackByPosLocked(queue_.queue_id, pos);
            if (!tr)
                return std::unexpected(tr.error());
            out = tr.value();
            return {};
        }
        size_t cnt = db_->queueCountLocked(queue_.queue_id);
        if (cnt == 0)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "empty queue"));
        if (queue_.cursor >= cnt) {
            if (queue_.repeat == RepeatMode::One) {
                size_t idx = cnt - 1;
                if (queue_.cursor > 0 && queue_.cursor <= cnt)
                    idx = queue_.cursor - 1;
                auto tr = fetchTrackByPosLocked(queue_.queue_id, (int64_t)idx);
                if (!tr)
                    return std::unexpected(tr.error());
                out = tr.value();
                return {};
            } else {
                // wrap for both Off and Queue; reshuffle if shuffle on
                if (queue_.shuffle) {
                    auto sr = setShuffleLocked(true);
                    if (!sr)
                        return std::unexpected(sr.error());
                    queue_.cursor = 0;
                } else {
                    queue_.cursor = 0;
                }
                (void)persistCursorLocked();
            }
        }
        auto tr = fetchTrackByPosLocked(queue_.queue_id, (int64_t)queue_.cursor);
        if (!tr)
            return std::unexpected(tr.error());
        queue_.cursor++;
        (void)persistCursorLocked();
        out = tr.value();
        return {};
    }

    /**
     * @brief Moves queue cursor to previous track per shuffle/repeat mode (queueMutex_ held).
     * @ingroup caudio_engine
     * @param out Output track reference (filled on success).
     * @return `std::expected<void, Error>` — success, NotFound if queue empty/at start/no perm.
     * @details Non-shuffle path:
     * - If cursor <= 1: returns NotFound ("at start" if cursor==0).
     * - Else: cursor -= 2, clamp to count-1, fetch track at cursor, increment cursor, persist cursor.
     * Shuffle path:
     * - If perm empty: NotFound ("no perm").
     * - If cursor <= 1: cursor=0 (or NotFound if cursor==0).
     * - Else: cursor -= 2, clamp to perm.size()-1, fetch track at perm[cursor], increment cursor, persist cursor.
     * @par Thread safety
     * Caller MUST hold `queueMutex_` (acquired via tryLockQueue()). Locks dbMutex_ shared for fetch,
     * exclusive via persistCursorLocked.
     * @see prev
     * @see queueNextLocked
     * @see persistCursorLocked
     */
    std::expected<void, caudio::utils::Error> queuePrevLocked(caudio::db::Track& out) {
        if (!queue_.shuffle) {
            size_t cnt = db_->queueCountLocked(queue_.queue_id);
            if (cnt == 0)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "empty queue"));
            if (queue_.cursor <= 1) {
                if (queue_.cursor == 0)
                    return std::unexpected(
                        caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "at start"));
                queue_.cursor = 0;
            } else {
                queue_.cursor -= 2;
                if (queue_.cursor >= cnt)
                    queue_.cursor = cnt - 1;
            }
            auto tr = fetchTrackByPosLocked(queue_.queue_id, (int64_t)queue_.cursor);
            if (!tr)
                return std::unexpected(tr.error());
            queue_.cursor++;
            (void)persistCursorLocked();
            out = tr.value();
            return {};
        }
        if (queue_.perm.empty())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "no perm"));
        if (queue_.cursor <= 1) {
            if (queue_.cursor == 0)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "at start"));
            queue_.cursor = 0;
        } else {
            queue_.cursor -= 2;
            if (queue_.cursor >= queue_.perm.size())
                queue_.cursor = queue_.perm.size() - 1;
        }
        int64_t pos = queue_.perm[queue_.cursor];
        queue_.cursor++;
        (void)persistCursorLocked();
        auto tr = fetchTrackByPosLocked(queue_.queue_id, pos);
        if (!tr)
            return std::unexpected(tr.error());
        out = tr.value();
        return {};
    }

    /**
     * @brief Initializes decoder/reader/ring/output for a track and starts playback.
     * @ingroup caudio_engine
     * @param t Track to play (path used to open reader/decoder).
     * @return `std::expected<void, Error>` — always success (errors stored in lastErr_).
     * @details Resets prior decoder/reader/ring/output. Attempts to open FileReader + DecoderRegistry
     * from track path. On success: sets duration_, creates SpscRing (8192*ch frames), AudioOutput,
     * calls preroll() to fill ring to half capacity, starts output. On failure: falls back to
     * metadata duration (no audio output), sets lastErr_. Updates state: currentTrack_, hasCurrent_=true,
     * markedPlayed_=false, gaplessArmed_=false, startedMs_=nowMs(), state_.currentTrackId, cursorPos,
     * playbackState_=Playing, playStart_=now, pausePos_=0. Calls saveState() if DB attached. Pushes
     * TrackStarted event. Notifies decodeCv_ and monCv_.
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
    std::expected<void, caudio::utils::Error> doPlayTrack(caudio::db::Track& t) {
        // try to open decoder path
        std::string path = t.path;
        // reset previous
        decoder_.reset();
        reader_.reset();
        ring_.reset();
        if (output_) {
            output_->stop();
            output_.reset();
        }

        // attempt to open reader & decoder if file exists; otherwise fallback to simulated
        bool opened = false;
        if (!path.empty()) {
            auto rRes = caudio::player::FileReader::open(path);
            if (rRes) {
                reader_ = std::move(rRes.value());
                auto dRes = caudio::player::DecoderRegistry::open(*reader_);
                if (dRes) {
                    decoder_ = std::move(dRes.value());
                    opened = true;
                    duration_ = decoder_->totalFrames() ? (double)decoder_->totalFrames() /
                                                              (double)decoder_->sampleRate()
                                                        : t.duration;
                    if (duration_ <= 0)
                        duration_ = t.duration > 0 ? t.duration : 1.0;
                    // create ring and output
                    uint32_t sr = decoder_->sampleRate();
                    uint32_t ch = decoder_->channels();
                    if (sr == 0)
                        sr = 48000;
                    if (ch == 0)
                        ch = 2;
                    ring_ = std::make_unique<caudio::utils::SpscRing<float>>(8192 * ch, ch);
                    caudio::player::AudioOutput::Config cfg;
                    cfg.sampleRate = sr;
                    cfg.channels = ch;
                    cfg.ring = ring_.get();
                    cfg.volume = volume_.load(std::memory_order_relaxed);
                    auto oRes = caudio::player::AudioOutput::create(cfg);
                    if (oRes) {
                        output_ = std::move(oRes.value());
                        // preroll: decode some frames before start
                        preroll();
                        output_->start();
                    } else {
                        lastErr_ = oRes.error().message;
                    }
                } else {
                    lastErr_ = dRes.error().message;
                }
            } else {
                lastErr_ = rRes.error().message;
            }
        }
        if (!opened) {
            // fallback to track metadata duration
            duration_ = t.duration > 0 ? t.duration : 1.0;
            // no ring/output needed for simulated playback
        }
        currentTrack_ = t;
        // update state
        hasCurrent_.store(true, std::memory_order_release);
        markedPlayed_.store(false, std::memory_order_release);
        gaplessArmed_.store(false, std::memory_order_release);
        startedMs_ = (int64_t)detail::nowMs();
        state_.currentTrackId = t.id;
        state_.cursorPos = (int64_t)queue_.cursor;
        playbackState_.store(PlaybackState::Playing, std::memory_order_release);
        playStart_ = std::chrono::steady_clock::now();
        pausePos_ = 0;
        if (hasDb())
            (void)saveState();
        EngineEvent ev;
        ev.type = EngineEventType::TrackStarted;
        ev.track_id = t.id;
        ev.queue_id = queue_.queue_id;
        ev.duration = duration_;
        ev.position = 0;
        pushEvent(ev);
        decodeCv_.notify_all();
        monCv_.notify_all();
        return {};
    }

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
    void preroll() {
        if (!decoder_ || !ring_)
            return;
        uint32_t ch = decoder_->channels();
        if (ch == 0)
            ch = 2;
        // preroll cap/2 frames like Player (ring capacity is in frames)
        size_t need = ring_->availableWrite() / 2;
        if (need == 0)
            return;
        size_t chunkFrames = 1024;
        size_t maxChunk = 2048 / ch;
        if (chunkFrames > maxChunk)
            chunkFrames = maxChunk;
        std::vector<float> tmp(chunkFrames * ch);
        while (need > 0 && ring_->availableWrite() >= tmp.size() / ch) {
            size_t frames = decoder_->decode(std::span<float>(tmp.data(), tmp.size()));
            if (frames == 0)
                break;
            size_t samples = frames * ch;
            size_t writtenFrames = ring_->write(std::span<float>(tmp.data(), samples));
            if (writtenFrames < frames)
                break;
            if (need > frames)
                need -= frames;
            else
                break;
        }
    }

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
     * Called from: play/next/prev/doPlayTrack (TrackStarted), engineTick (Progress/TrackEnded via gapless),
     * setShuffle/switchQueue (QueueChanged).
     * @see eventQueue_
     * @see EngineCallbacks
     * @see EngineEventType
     */
    void pushEvent(const EngineEvent& ev) {
        // MpscQueue cap 64 drop policy
        auto r = eventQueue_.push(ev);
        if (!r)
            return; // drop if full
        // dispatch callbacks outside queue lock
        EngineCallbacks cbsCopy;
        {
            std::lock_guard<std::mutex> lk(cbMutex_);
            cbsCopy = callbacks_;
        }
        if (ev.type == EngineEventType::TrackStarted && cbsCopy.on_track_started) {
            cbsCopy.on_track_started(ev.track_id);
        } else if (ev.type == EngineEventType::TrackEnded && cbsCopy.on_track_ended) {
            double pct = 0;
            if (ev.duration > 0) {
                pct = (ev.position / ev.duration) * 100;
                if (pct < 0)
                    pct = 0;
                if (pct > 100)
                    pct = 100;
            }
            cbsCopy.on_track_ended(ev.track_id, pct);
        } else if (ev.type == EngineEventType::QueueChanged && cbsCopy.on_queue_changed) {
            cbsCopy.on_queue_changed(ev.queue_id);
        } else if (ev.type == EngineEventType::Error && cbsCopy.on_error) {
            cbsCopy.on_error(caudio::utils::StatusCode::Internal, ev.msg);
        }
    }

    /**
     * @brief Marks current track as played in history if thresholds met (exactly-once via CAS).
     * @ingroup caudio_engine
     * @details Called from engineTick(). Early exits if: no DB, no current track, already marked.
     * Checks thresholds via detail::shouldMarkPlayedEx(duration, position, false, pct, secs)
     * using config historyThresholdPct (default 60) and historyThresholdSecs (default 90).
     * Uses atomic CAS on markedPlayed_ (false->true) for exactly-once semantics.
     * Inside transaction (BEGIN IMMEDIATE): fetches track play_count, updates tracks
     * (play_count+1, last_played=now), inserts into history (track_id, started_at,
     * completed_at, position_ms, completion_pct, queue_id). Rolls back on any failure,
     * resets markedPlayed_=false. On commit success, markedPlayed_ remains true.
     * @par Thread safety
     * Called from monitorLoop (single-threaded). Locks dbMutex_ exclusively.
     * @see engineTick
     * @see monitorLoop
     * @see detail::shouldMarkPlayedEx
     */
    void doHistoryMark() {
        if (!hasDb())
            return;
        if (!hasCurrent_.load(std::memory_order_acquire))
            return;
        if (markedPlayed_.load(std::memory_order_acquire))
            return;
        double dur = duration_;
        double pos = currentPositionLocked();
        int pct = cfg_.historyThresholdPct ? cfg_.historyThresholdPct : 60;
        int secs = cfg_.historyThresholdSecs ? cfg_.historyThresholdSecs : 90;
        if (!detail::shouldMarkPlayedEx(dur, pos, false, pct, secs))
            return;
        bool expected = false;
        bool desired = true;
        // CAS exactly-once
        bool exchanged = markedPlayed_.compare_exchange_strong(
            expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
        if (!exchanged)
            return;
        // transaction
        auto* h = dbHandle();
        auto* m = dbMutex();
        if (!h || !m) {
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        std::unique_lock<std::shared_mutex> lk(*m);
        char* err = nullptr;
        int rc = sqlite3_exec(h, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        SqliteErrGuard errGuard{err};
        if (rc != SQLITE_OK) {
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        // fetch track
        bool ok = true;
        int64_t playCount = 0;
        {
            sqlite3_stmt* raw = nullptr;
            const char* selSql = "SELECT id, play_count FROM tracks WHERE id=?";
            rc = sqlite3_prepare_v2(h, selSql, -1, &raw, nullptr);
            StmtGuard guard(raw);
            if (rc != SQLITE_OK)
                ok = false;
            else {
                raw = guard.get();
                sqlite3_bind_int64(raw, 1, currentTrack_.id);
                if (sqlite3_step(raw) == SQLITE_ROW)
                    playCount = sqlite3_column_int64(raw, 1);
                else
                    ok = false;
            }
        }
        if (!ok) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        // update track
        {
            sqlite3_stmt* raw = nullptr;
            const char* updSql = "UPDATE tracks SET play_count=?, last_played=? WHERE id=?";
            rc = sqlite3_prepare_v2(h, updSql, -1, &raw, nullptr);
            StmtGuard guard(raw);
            if (rc != SQLITE_OK)
                ok = false;
            else {
                raw = guard.get();
                int64_t nowSec = (int64_t)(detail::nowMs() / 1000);
                sqlite3_bind_int64(raw, 1, playCount + 1);
                sqlite3_bind_int64(raw, 2, nowSec);
                sqlite3_bind_int64(raw, 3, currentTrack_.id);
                rc = sqlite3_step(raw);
                if (rc != SQLITE_DONE)
                    ok = false;
            }
        }
        if (!ok) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        // history insert
        {
            sqlite3_stmt* raw = nullptr;
            const char* insSql = "INSERT INTO history (track_id, started_at, completed_at, "
                                 "position_ms, completion_pct, queue_id) VALUES (?,?,?,?,?,?)";
            rc = sqlite3_prepare_v2(h, insSql, -1, &raw, nullptr);
            StmtGuard guard(raw);
            if (rc != SQLITE_OK)
                ok = false;
            else {
                raw = guard.get();
                int64_t posMs = (int64_t)(pos * 1000.0);
                double compPct = 0;
                if (dur > 0) {
                    compPct = (pos / dur) * 100;
                    if (compPct > 100)
                        compPct = 100;
                }
                sqlite3_bind_int64(raw, 1, currentTrack_.id);
                sqlite3_bind_int64(raw, 2, startedMs_);
                sqlite3_bind_int64(raw, 3, (int64_t)detail::nowMs());
                sqlite3_bind_int64(raw, 4, posMs);
                sqlite3_bind_double(raw, 5, compPct);
                sqlite3_bind_int64(raw, 6, queue_.queue_id ? queue_.queue_id : 1);
                rc = sqlite3_step(raw);
                if (rc != SQLITE_DONE)
                    ok = false;
            }
        }
        if (!ok) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        char* commitErr = nullptr;
        rc = sqlite3_exec(h, "COMMIT", nullptr, nullptr, &commitErr);
        SqliteErrGuard commitGuard{commitErr};
        if (rc != SQLITE_OK) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        // success keep marked true
    }

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
    void engineTick() {
        doHistoryMark();
        auto st = playbackState_.load(std::memory_order_acquire);
        if (st == PlaybackState::Playing) {
            uint64_t now = detail::nowMs();
            uint64_t last = lastProgressMs_.load(std::memory_order_acquire);
            if (now - last >= 500) {
                lastProgressMs_.store(now, std::memory_order_release);
                EngineEvent ev;
                ev.type = EngineEventType::Progress;
                ev.track_id = hasCurrent_.load(std::memory_order_acquire) ? currentTrack_.id
                                                                          : state_.currentTrackId;
                ev.queue_id = queue_.queue_id ? queue_.queue_id : 1;
                ev.position = currentPositionLocked();
                ev.duration = duration_;
                pushEvent(ev);
            }
            if (duration_ > 0) {
                double pos = currentPositionLocked();
                double remaining = duration_ - pos;
                double gapS = (double)cfg_.gaplessMs / 1000.0;
                if (remaining <= gapS && remaining >= 0.0) {
                    if (hasCurrent_.load(std::memory_order_acquire)) {
                        // gaplessArmed 0→1 CAS, 300ms preroll
                        bool expected = false;
                        if (gaplessArmed_.compare_exchange_strong(expected, true,
                                                                  std::memory_order_acq_rel,
                                                                  std::memory_order_acquire)) {
                            auto nr = next();
                            if (!nr)
                                gaplessArmed_.store(false, std::memory_order_release);
                        }
                    }
                }
            }
        }
    }

    /**
     * @brief Monitor thread main loop — polls engineTick at configurable interval.
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
    void monitorLoop(std::stop_token st) {
        int poll = cfg_.pollMs ? cfg_.pollMs : 10;
        if (poll <= 0)
            poll = 10;
        std::unique_lock<std::mutex> lk(monMtx_);
        while (!st.stop_requested() && monRun_.load(std::memory_order_acquire)) {
            lk.unlock();
            engineTick();
            lk.lock();
            monCv_.wait_for(lk, std::chrono::milliseconds(poll), [&st, this] {
                return st.stop_requested() || !monRun_.load(std::memory_order_acquire);
            });
        }
    }

    /**
     * @brief Decode thread main loop — fills SPSC ring with decoded audio frames.
     * @ingroup caudio_engine
     * @param st Stop token from jthread (request_stop() on shutdown).
     * @details Runs while decodeRun_ is true and stop not requested.
     * - If not Playing or no decoder/ring: waits on decodeCv_ (10 ms timeout) for state change.
     * - If ring full: sleeps 5 ms and retries.
     * - Computes max frames to decode (avail/ch, capped at 1024 frames, max 2048/ch).
     * - Decodes under decodeMtx_ (re-checks Playing state to avoid race with seek()).
     * - On EOF (frames==0): sleeps 10 ms, lets monitorLoop handle gapless/next.
     * - Writes decoded frames to ring under decodeMtx_ (re-checks Playing to avoid race with seek() ring reset).
     * SPSC ring (ring_) is written by decodeLoop only; read by AudioOutput callback.
     * @par Thread safety
     * Runs on dedicated decodeThread_ (jthread). Single writer to ring_.
     * Uses decodeMtx_ to serialize with seek()/doPlayTrack() decoder/ring access.
     * Waits on decodeCv_ notified by play/pause/resume/stop/seek/next/prev.
     * @see init
     * @see shutdown
     * @see decodeMtx_
     * @see decodeCv_
     * @see caudio::utils::SpscRing
     * @see caudio::player::IDecoder
     */
    void decodeLoop(std::stop_token st) {
        constexpr std::size_t kMaxChunkFrames = 1024;
        while (!st.stop_requested() && decodeRun_.load(std::memory_order_acquire)) {
            auto ps = playbackState_.load(std::memory_order_acquire);
            if (ps != PlaybackState::Playing || !decoder_ || !ring_) {
                std::unique_lock<std::mutex> lk(decodeMtx_);
                decodeCv_.wait_for(lk, std::chrono::milliseconds(10), [&st, this] {
                    return st.stop_requested() ||
                           playbackState_.load(std::memory_order_acquire) == PlaybackState::Playing;
                });
                continue;
            }
            size_t avail = ring_->availableWrite();
            if (avail == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            uint32_t ch = decoder_->channels();
            if (ch == 0)
                ch = 2;
            size_t maxFrames = avail / ch;
            if (maxFrames > kMaxChunkFrames)
                maxFrames = kMaxChunkFrames;
            size_t maxChunkByCh = 2048 / ch;
            if (maxFrames > maxChunkByCh)
                maxFrames = maxChunkByCh;
            if (maxFrames == 0)
                maxFrames = 1;

            std::vector<float> buf(maxFrames * ch);
            size_t frames;
            {
                std::unique_lock<std::mutex> lk(decodeMtx_);
                // Re-check state under lock to avoid race with seek()
                if (playbackState_.load(std::memory_order_acquire) != PlaybackState::Playing)
                    continue;
                frames = decoder_->decode(std::span<float>(buf.data(), buf.size()));
            }
            if (frames == 0) {
                // EOF reached - wait a bit, let monitor handle next (gapless) or stop
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            size_t samples = frames * ch;
            size_t writtenFrames;
            {
                std::unique_lock<std::mutex> lk(decodeMtx_);
                // Re-check state under lock to avoid race with seek() ring reset
                if (playbackState_.load(std::memory_order_acquire) != PlaybackState::Playing)
                    continue;
                writtenFrames = ring_->write(std::span<float>(buf.data(), samples));
            }
            if (writtenFrames < frames) {
                // Ring full, will retry next iteration
            }
        }
    }

    // members
    EngineConfig cfg_{};
    EngineState state_{};
    QueueState queue_{};
    caudio::db::Track currentTrack_{};
    std::atomic<bool> hasCurrent_{false};
    double duration_{0};
    std::atomic<float> volume_{1.0f};
    std::atomic<PlaybackState> playbackState_{PlaybackState::Stopped};
    std::chrono::steady_clock::time_point playStart_{};
    double pausePos_{0};
    std::atomic<bool> markedPlayed_{false};
    int64_t startedMs_{0};
    std::string lastErr_{};

    std::shared_ptr<caudio::db::Database> db_{};

    // player
    std::unique_ptr<caudio::player::Reader> reader_{};
    std::unique_ptr<caudio::player::IDecoder> decoder_{};
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
    // gaplessArmed_: 0→1 CAS arms gapless pre-roll ~300ms before track end (gaplessMs).
    // Reset to false on TrackStarted / next() failure. Requires engineTick() single-writer.
    std::atomic<bool> gaplessArmed_{false};
    mutable std::mutex queueMutex_;
};

} // namespace caudio::engine
