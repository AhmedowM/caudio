module;
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

#include "../../vendor/sqlite3.h"

export module caudio.engine;

export import :types;
export import :history_policy;
export import :queue_logic;

import caudio.utils;
import caudio.player;
import caudio.db;

export namespace caudio::engine {

constexpr std::string_view toString(caudio::utils::Result r) noexcept {
    return caudio::utils::toString(r);
}

class Engine final {
  public:
    using ExpectedVoid = std::expected<void, caudio::utils::Error>;
    using ExpectedEngine = std::expected<std::unique_ptr<Engine>, caudio::utils::Error>;

    static ExpectedEngine create(const EngineConfig& cfg = {}) {
        auto e = std::unique_ptr<Engine>(new Engine(cfg));
        auto err = e->init();
        if (err)
            return std::unexpected(*err);
        return e;
    }

    static ExpectedEngine open(std::string_view path, const EngineConfig& cfg = {}) {
        auto dbRes = caudio::db::Database::open(path);
        if (!dbRes)
            return std::unexpected(dbRes.error());
        auto e = std::unique_ptr<Engine>(new Engine(cfg));
        e->db_ = std::move(dbRes.value());
        e->ownsDb_ = true;
        if (auto err = e->init())
            return std::unexpected(*err);
        // load state after init so monitor already running but that's ok
        (void)e->loadState();
        // apply volume to output if any
        if (e->output_)
            e->output_->setVolume(e->state_.volume);
        return e;
    }

    ExpectedVoid attachDatabase(std::shared_ptr<caudio::db::Database> db) {
        if (!db || !db->handle())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "null db"));
        // need to handle unique_ptr vs shared_ptr: we store raw handle via shared ownership wrapper
        // Create a new Database unique_ptr that wraps same handle is unsafe. Instead store shared.
        // For compatibility, we keep dbShared_ and use dbShared_->handle()
        dbShared_ = std::move(db);
        // also need to keep a non-owning pointer for legacy loadState that expects unique_ptr? we
        // use dbShared_
        if (auto ec = loadStateFromShared())
            return std::unexpected(*ec);
        return {};
    }

    // Overload for unique_ptr
    ExpectedVoid attachDb(std::unique_ptr<caudio::db::Database> db) {
        if (!db || !db->handle())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "null db"));
        db_ = std::move(db);
        ownsDb_ = false;
        if (auto ec = loadState())
            return std::unexpected(*ec);
        return {};
    }

    ~Engine() {
        shutdown();
    }

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
        } else if (dbShared_ && dbShared_->handle()) {
            state_.cursorPos = (int64_t)queue_.cursor;
            (void)saveStateShared();
        }
        if (output_) {
            output_->stop();
            output_.reset();
        }
        decoder_.reset();
        reader_.reset();
        ring_.reset();
    }

    void destroy() {
        shutdown();
    }

    // Playback
    ExpectedVoid play(int64_t queueId = 1) {
        if (!hasDb())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::State, "no db"));
        if (queueId == 0)
            queueId = 1;
        if (!tryLockQueue())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::Busy, "queue busy"));
        queue_.queueId = queueId;
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

    ExpectedVoid pause() {
        auto s = playbackState_.load(std::memory_order_acquire);
        if (s != PlaybackState::Playing)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::State, "not playing"));
        // record pause position
        pausePos_ = currentPositionLocked();
        playbackState_.store(PlaybackState::Paused, std::memory_order_release);
        if (output_)
            output_->stop();
        return {};
    }

    ExpectedVoid resume() {
        auto s = playbackState_.load(std::memory_order_acquire);
        if (s != PlaybackState::Paused)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::State, "not paused"));
        playStart_ = std::chrono::steady_clock::now();
        playbackState_.store(PlaybackState::Playing, std::memory_order_release);
        if (output_)
            output_->start();
        decodeCv_.notify_all();
        monCv_.notify_all();
        return {};
    }

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

    ExpectedVoid seek(double seconds) {
        if (!hasCurrent_.load(std::memory_order_acquire))
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::State, "no track"));
        if (!std::isfinite(seconds) || seconds < 0)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "bad seconds"));
        if (decoder_) {
            auto res = decoder_->seek(seconds);
            if (!res)
                return std::unexpected(res.error());
        }
        // adjust position tracking
        double dur = duration_;
        if (seconds > dur)
            seconds = dur;
        pausePos_ = seconds;
        playStart_ = std::chrono::steady_clock::now();
        // if paused, keep paused pos
        auto s = playbackState_.load(std::memory_order_acquire);
        if (s == PlaybackState::Paused) {
            pausePos_ = seconds;
        }
        if (ring_)
            ring_->reset();
        decodeCv_.notify_all();
        return {};
    }

    ExpectedVoid setVolume(float g) {
        if (!std::isfinite(g))
            g = 0.0f;
        if (g < 0.0f)
            g = 0.0f;
        if (g > 1.0f)
            g = 1.0f;
        state_.volume = g;
        volume_.store(g, std::memory_order_relaxed);
        if (output_)
            output_->setVolume(g);
        if (hasDb()) {
            state_.cursorPos = (int64_t)queue_.cursor;
            if (db_)
                (void)saveState();
            else
                (void)saveStateShared();
        }
        return {};
    }

    float volume() const noexcept {
        return volume_.load(std::memory_order_relaxed);
    }
    PlaybackState state() const noexcept {
        return playbackState_.load(std::memory_order_acquire);
    }
    double duration() const noexcept {
        return duration_;
    }
    double position() const noexcept {
        return currentPositionLocked();
    }
    int64_t currentTrackId() const noexcept {
        if (hasCurrent_.load(std::memory_order_acquire))
            return currentTrack_.id;
        return state_.currentTrackId;
    }

    std::expected<caudio::db::DbStats, caudio::utils::Error> getStats() {
        if (!hasDb())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::State, "no db"));
        if (db_)
            return db_->getStats();
        return dbShared_->getStats();
    }

    std::string lastError() const {
        if (!lastErr_.empty())
            return lastErr_;
        return "";
    }

    ExpectedVoid setShuffle(bool on) {
        if (!hasDb())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::State, "no db"));
        if (!tryLockQueue())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Busy, "busy"));
        auto r = setShuffleLocked(on);
        unlockQueue();
        if (!r)
            return std::unexpected(r.error());
        // push queue changed event
        EngineEvent ev;
        ev.type = EngineEventType::QueueChanged;
        ev.queueId = queue_.queueId;
        pushEvent(ev);
        return {};
    }

    ExpectedVoid setRepeat(RepeatMode m) {
        if (m != RepeatMode::Off && m != RepeatMode::Queue && m != RepeatMode::One)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "bad repeat"));
        if (!tryLockQueue())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Busy, "busy"));
        queue_.repeat = m;
        state_.repeatMode = m;
        state_.cursorPos = (int64_t)queue_.cursor;
        std::optional<caudio::utils::Error> err;
        if (db_) {
            if (auto e = saveState(); !e)
                err = e.error();
        } else {
            if (auto e = saveStateShared(); !e)
                err = e.error();
        }
        unlockQueue();
        if (err)
            return std::unexpected(*err);
        return {};
    }

    ExpectedVoid next() {
        if (!hasDb())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::State, "no db"));
        // handle repeat one without shuffle without queue lock? match C: if !shuffle && repeat==One
        // && hasCurrent => seek 0 and play
        if (!queue_.shuffle && queue_.repeat == RepeatMode::One &&
            hasCurrent_.load(std::memory_order_acquire)) {
            // seek to 0
            if (decoder_)
                (void)decoder_->seek(0.0);
            pausePos_ = 0;
            playStart_ = std::chrono::steady_clock::now();
            playbackState_.store(PlaybackState::Playing, std::memory_order_release);
            markedPlayed_.store(false, std::memory_order_release);
            gaplessArmed_.store(false, std::memory_order_release);
            startedMs_ = (int64_t)detail::nowMs();
            EngineEvent ev;
            ev.type = EngineEventType::TrackStarted;
            ev.trackId = currentTrack_.id;
            ev.queueId = queue_.queueId;
            ev.duration = duration_;
            pushEvent(ev);
            decodeCv_.notify_all();
            return {};
        }
        if (!tryLockQueue())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Busy, "busy"));
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

    ExpectedVoid prev() {
        if (!hasDb())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::State, "no db"));
        if (!tryLockQueue())
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Busy, "busy"));
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

    ExpectedVoid setCallbacks(const EngineCallbacks& cbs) {
        std::lock_guard<std::mutex> lk(cbMutex_);
        callbacks_ = cbs;
        cfg_.callbacks = cbs;
        return {};
    }

    std::expected<EngineEvent, caudio::utils::Error> pollEvent() {
        auto r = eventQueue_.pop();
        if (!r)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::NotFound, "no event"));
        return r.value();
    }

    ExpectedVoid drainEvents(EngineEvent* buf, size_t cap, size_t* n) {
        if (!n)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "null n"));
        *n = 0;
        if (!buf && cap != 0)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "null buf"));
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

    // compatibility helpers for C API naming
    PlaybackState getState() const noexcept {
        return state();
    }
    double getPosition() const noexcept {
        return position();
    }

  private:
    explicit Engine(const EngineConfig& cfg) : cfg_(cfg), state_{}, queue_{} {
        cfg_.pollMs = cfg.pollMs ? cfg.pollMs : 10;
        cfg_.gaplessMs = cfg.gaplessMs ? cfg.gaplessMs : 300;
        cfg_.historyThresholdPct = cfg.historyThresholdPct ? cfg.historyThresholdPct : 60;
        cfg_.historyThresholdSecs = cfg.historyThresholdSecs ? cfg.historyThresholdSecs : 90;
        state_.volume = 1.0f;
        volume_.store(1.0f, std::memory_order_relaxed);
        state_.repeatMode = RepeatMode::Off;
        queue_.repeat = RepeatMode::Off;
        queue_.queueId = 1;
        playbackState_.store(PlaybackState::Stopped, std::memory_order_release);
        hasCurrent_.store(false, std::memory_order_release);
        markedPlayed_.store(false, std::memory_order_release);
        gaplessArmed_.store(false, std::memory_order_release);
        lastProgressMs_.store(0, std::memory_order_release);
        queueLock_.store(0, std::memory_order_release);
        // callbacks from config
        callbacks_ = cfg.callbacks;
    }

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

    bool hasDb() const noexcept {
        return (db_ && db_->handle()) || (dbShared_ && dbShared_->handle());
    }

    bool tryLockQueue() noexcept {
        int expected = 0;
        return queueLock_.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                                  std::memory_order_acquire);
    }
    void unlockQueue() noexcept {
        queueLock_.store(0, std::memory_order_release);
    }

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
    sqlite3* dbHandle() const noexcept {
        if (db_)
            return db_->handle();
        if (dbShared_)
            return dbShared_->handle();
        return nullptr;
    }
    std::shared_mutex* dbMutex() const noexcept {
        if (db_)
            return &db_->mutex();
        if (dbShared_)
            return &dbShared_->mutex();
        return nullptr;
    }

    template <typename Fn>
    std::expected<void, caudio::utils::Error> withTransaction(Fn&& fn) {
        auto* h = dbHandle();
        auto* m = dbMutex();
        if (!h || !m)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "no db"));
        std::unique_lock<std::shared_mutex> lk(*m);
        char* err = nullptr;
        int rc = sqlite3_exec(h, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (err) {
            sqlite3_free(err);
            err = nullptr;
        }
        if (rc != SQLITE_OK)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::Busy, "begin failed"));

        auto result = fn(h);
        if (!result) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            return result;
        }
        rc = sqlite3_exec(h, "COMMIT", nullptr, nullptr, &err);
        if (err)
            sqlite3_free(err);
        if (rc != SQLITE_OK) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::Internal, "commit failed"));
        }
        return {};
    }

    std::optional<caudio::utils::Error> loadState() {
        auto* h = dbHandle();
        auto* m = dbMutex();
        if (!h || !m)
            return caudio::utils::makeError(caudio::utils::Result::InvalidArg, "no db");
        std::unique_lock<std::shared_mutex> lk(*m);
        const char* sql = "SELECT shuffle_enabled, repeat_mode, cursor_pos, current_track_id, "
                          "volume, shuffle_perm FROM engine_state WHERE id=1";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(h, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            if (stmt)
                sqlite3_finalize(stmt);
            return caudio::utils::makeError(caudio::utils::Result::Internal, "prepare failed");
        }
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            state_.shuffleEnabled = sqlite3_column_int(stmt, 0);
            state_.repeatMode = (RepeatMode)sqlite3_column_int(stmt, 1);
            state_.cursorPos = sqlite3_column_int64(stmt, 2);
            state_.currentTrackId = sqlite3_column_int64(stmt, 3);
            state_.volume = (float)sqlite3_column_double(stmt, 4);
            if (state_.volume < 0 || state_.volume > 1)
                state_.volume = 1.0f;
            volume_.store(state_.volume, std::memory_order_relaxed);
            const void* blob = sqlite3_column_blob(stmt, 5);
            int blobBytes = sqlite3_column_bytes(stmt, 5);
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
                else if (queue_.perm.empty() && queue_.cursor != 0)
                    queue_.cursor = 0;
            }
            queue_.queueId = 1;
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        sqlite3_finalize(stmt);
        if (rc == SQLITE_DONE) {
            state_.shuffleEnabled = 0;
            state_.repeatMode = RepeatMode::Off;
            state_.cursorPos = 0;
            state_.currentTrackId = 0;
            state_.volume = 1.0f;
            volume_.store(1.0f, std::memory_order_relaxed);
            queue_.perm.clear();
            queue_.cursor = 0;
            queue_.shuffle = false;
            queue_.repeat = RepeatMode::Off;
            queue_.queueId = 1;
            return std::nullopt;
        }
        return caudio::utils::makeError(caudio::utils::Result::Internal, "load failed");
    }

    std::optional<caudio::utils::Error> loadStateFromShared() {
        return loadState();
    }

    std::expected<void, caudio::utils::Error> saveState() {
        return withTransaction([&](sqlite3* db) -> std::expected<void, caudio::utils::Error> {
            const char* sql =
                "UPDATE engine_state SET shuffle_enabled=?, repeat_mode=?, shuffle_perm=?, "
                "cursor_pos=?, current_track_id=?, volume=?, updated=CURRENT_TIMESTAMP WHERE id=1";
            sqlite3_stmt* stmt = nullptr;
            int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
            if (rc != SQLITE_OK)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::Internal, "prepare failed"));
            sqlite3_bind_int(stmt, 1, state_.shuffleEnabled);
            sqlite3_bind_int(stmt, 2, (int)state_.repeatMode);
            if (queue_.shuffle && !queue_.perm.empty()) {
                if (queue_.perm.size() >
                    (size_t)(std::numeric_limits<int>::max() / (int)sizeof(int64_t))) {
                    sqlite3_finalize(stmt);
                    return std::unexpected(
                        caudio::utils::makeError(caudio::utils::Result::NoMem, "perm too large"));
                }
                sqlite3_bind_blob(stmt, 3, queue_.perm.data(),
                                  (int)(queue_.perm.size() * sizeof(int64_t)), SQLITE_TRANSIENT);
            } else
                sqlite3_bind_null(stmt, 3);
            sqlite3_bind_int64(stmt, 4, state_.cursorPos);
            sqlite3_bind_int64(stmt, 5, state_.currentTrackId);
            sqlite3_bind_double(stmt, 6, (double)state_.volume);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc != SQLITE_DONE)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::Internal, "step failed"));
            return {};
        });
    }
    std::expected<void, caudio::utils::Error> saveStateShared() {
        return saveState();
    }

    // Queue helpers
    size_t queueCountLocked(int64_t qid) {
        if (qid == 0)
            qid = 1;
        auto* h = dbHandle();
        auto* m = dbMutex();
        if (!h || !m)
            return 0;
        std::shared_lock<std::shared_mutex> lk(*m);
        const char* sql = "SELECT COUNT(*) FROM queue WHERE queue_id=?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(h, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return 0;
        sqlite3_bind_int64(stmt, 1, qid);
        size_t cnt = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            cnt = (size_t)sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return cnt;
    }

    std::expected<void, caudio::utils::Error> persistShuffleBlobLocked() {
        return withTransaction([&](sqlite3* db) -> std::expected<void, caudio::utils::Error> {
            const char* sql =
                "UPDATE engine_state SET shuffle_perm=?, cursor_pos=?, shuffle_enabled=? WHERE id=1";
            sqlite3_stmt* stmt = nullptr;
            int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
            if (rc != SQLITE_OK)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::Internal, "prepare failed"));
            if (queue_.shuffle && !queue_.perm.empty()) {
                if (queue_.perm.size() >
                    (size_t)(std::numeric_limits<int>::max() / (int)sizeof(int64_t))) {
                    sqlite3_finalize(stmt);
                    return std::unexpected(
                        caudio::utils::makeError(caudio::utils::Result::NoMem, "perm large"));
                }
                sqlite3_bind_blob(stmt, 1, queue_.perm.data(),
                                  (int)(queue_.perm.size() * sizeof(int64_t)), SQLITE_TRANSIENT);
            } else
                sqlite3_bind_null(stmt, 1);
            sqlite3_bind_int64(stmt, 2, (int64_t)queue_.cursor);
            sqlite3_bind_int(stmt, 3, queue_.shuffle ? 1 : 0);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc != SQLITE_DONE)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::Internal, "step failed"));
            // also update state
            state_.shuffleEnabled = queue_.shuffle ? 1 : 0;
            state_.cursorPos = (int64_t)queue_.cursor;
            return {};
        });
    }

    std::expected<void, caudio::utils::Error> persistCursorLocked() {
        return withTransaction([&](sqlite3* db) -> std::expected<void, caudio::utils::Error> {
            const char* sql = "UPDATE engine_state SET cursor_pos=? WHERE id=1";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::Internal, "prepare failed"));
            sqlite3_bind_int64(stmt, 1, (int64_t)queue_.cursor);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc != SQLITE_DONE)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::Internal, "step failed"));
            state_.cursorPos = (int64_t)queue_.cursor;
            return {};
        });
    }

    std::expected<caudio::db::Track, caudio::utils::Error> fetchTrackByPosLocked(int64_t qid,
                                                                                 int64_t pos) {
        if (qid == 0)
            qid = 1;
        auto* h = dbHandle();
        auto* m = dbMutex();
        if (!h || !m)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "no db"));
        int64_t trackId = 0;
        {
            std::shared_lock<std::shared_mutex> lk(*m);
            const char* sql =
                "SELECT track_id FROM queue WHERE queue_id=? ORDER BY position LIMIT 1 OFFSET ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(h, sql, -1, &stmt, nullptr) != SQLITE_OK)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::Internal, "prepare failed"));
            sqlite3_bind_int64(stmt, 1, qid);
            sqlite3_bind_int64(stmt, 2, pos);
            int rc = sqlite3_step(stmt);
            if (rc != SQLITE_ROW) {
                sqlite3_finalize(stmt);
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::NotFound, "not found"));
            }
            trackId = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
        }
        // fetch track via db API (which will lock internally but we released lock)
        if (db_) {
            auto tr = db_->getTrack(trackId);
            if (!tr)
                return std::unexpected(tr.error());
            return tr.value();
        } else {
            auto tr = dbShared_->getTrack(trackId);
            if (!tr)
                return std::unexpected(tr.error());
            return tr.value();
        }
    }

    std::expected<void, caudio::utils::Error> setShuffleLocked(bool on) {
        bool want = on;
        if (queue_.shuffle == want && !queue_.perm.empty())
            return {};
        if (want) {
            queue_.perm.clear();
            queue_.cursor = 0;
            size_t cnt = queueCountLocked(queue_.queueId);
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
                shufflePerm(queue_.perm, rng);
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

    std::expected<void, caudio::utils::Error> queueNextLocked(caudio::db::Track& out) {
        if (queue_.queueId == 0)
            queue_.queueId = 1;
        if (queue_.shuffle) {
            if (queue_.perm.empty()) {
                size_t cnt = queueCountLocked(queue_.queueId);
                if (cnt == 0)
                    return std::unexpected(
                        caudio::utils::makeError(caudio::utils::Result::NotFound, "empty queue"));
                auto sr = setShuffleLocked(true);
                if (!sr)
                    return std::unexpected(sr.error());
                if (queue_.perm.empty())
                    return std::unexpected(
                        caudio::utils::makeError(caudio::utils::Result::Internal, "no perm"));
            }
            if (queue_.cursor >= queue_.perm.size()) {
                if (queue_.repeat == RepeatMode::Queue) {
                    queue_.cursor = 0;
                    (void)persistCursorLocked();
                } else if (queue_.repeat == RepeatMode::One) {
                    size_t idx = queue_.perm.size() - 1;
                    if (queue_.cursor > 0 && queue_.cursor <= queue_.perm.size())
                        idx = queue_.cursor - 1;
                    int64_t pos = queue_.perm[idx];
                    auto tr = fetchTrackByPosLocked(queue_.queueId, pos);
                    if (!tr)
                        return std::unexpected(tr.error());
                    out = tr.value();
                    return {};
                } else {
                    return std::unexpected(
                        caudio::utils::makeError(caudio::utils::Result::NotFound, "end of queue"));
                }
            }
            int64_t pos = queue_.perm[queue_.cursor];
            queue_.cursor++;
            (void)persistCursorLocked();
            auto tr = fetchTrackByPosLocked(queue_.queueId, pos);
            if (!tr)
                return std::unexpected(tr.error());
            out = tr.value();
            return {};
        }
        size_t cnt = queueCountLocked(queue_.queueId);
        if (cnt == 0)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::NotFound, "empty queue"));
        if (queue_.repeat == RepeatMode::Queue) {
            return withTransaction([&](sqlite3* h) -> std::expected<void, caudio::utils::Error> {
                caudio::db::QueueItem qi;
                std::expected<caudio::db::QueueItem, caudio::utils::Error> dq;
                if (db_)
                    dq = db_->queueDequeueLocked(queue_.queueId);
                else
                    dq = dbShared_->queueDequeueLocked(queue_.queueId);
                if (!dq)
                    return std::unexpected(dq.error());
                qi = dq.value();
                if (db_)
                    (void)db_->queueEnqueueLocked(queue_.queueId, qi.trackId, -1);
                else
                    (void)dbShared_->queueEnqueueLocked(queue_.queueId, qi.trackId, -1);
                auto tr = db_ ? db_->getTrackLocked(qi.trackId) : dbShared_->getTrackLocked(qi.trackId);
                if (!tr)
                    return std::unexpected(tr.error());
                out = tr.value();
                return {};
            });
        }
        // normal: dequeue
        {
            auto dq =
                db_ ? db_->queueDequeue(queue_.queueId) : dbShared_->queueDequeue(queue_.queueId);
            if (!dq)
                return std::unexpected(dq.error());
            auto tr =
                db_ ? db_->getTrack(dq.value().trackId) : dbShared_->getTrack(dq.value().trackId);
            if (!tr)
                return std::unexpected(tr.error());
            out = tr.value();
            return {};
        }
    }

    std::expected<void, caudio::utils::Error> queuePrevLocked(caudio::db::Track& out) {
        if (!queue_.shuffle)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::NotFound, "prev only shuffle"));
        if (queue_.perm.empty())
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::NotFound, "no perm"));
        if (queue_.cursor <= 1) {
            if (queue_.cursor == 0)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::Result::NotFound, "at start"));
            queue_.cursor = 0;
        } else {
            queue_.cursor -= 2;
            if (queue_.cursor >= queue_.perm.size())
                queue_.cursor = queue_.perm.size() - 1;
        }
        int64_t pos = queue_.perm[queue_.cursor];
        queue_.cursor++;
        (void)persistCursorLocked();
        auto tr = fetchTrackByPosLocked(queue_.queueId, pos);
        if (!tr)
            return std::unexpected(tr.error());
        out = tr.value();
        return {};
    }

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
        // persist
        if (hasDb()) {
            if (db_)
                (void)saveState();
            else
                (void)saveStateShared();
        }
        // push event
        EngineEvent ev;
        ev.type = EngineEventType::TrackStarted;
        ev.trackId = t.id;
        ev.queueId = queue_.queueId;
        ev.duration = duration_;
        ev.position = 0;
        pushEvent(ev);
        // wake decode
        decodeCv_.notify_all();
        monCv_.notify_all();
        return {};
    }

    void preroll() {
        if (!decoder_ || !ring_)
            return;
        // fill up to half capacity
        size_t need = ring_->availableWrite() / 2;
        if (need == 0)
            return;
        std::vector<float> tmp(1024 * std::max<uint32_t>(1, decoder_->channels()));
        while (need > 0 && ring_->availableWrite() >= tmp.size() / decoder_->channels()) {
            size_t frames = decoder_->decode(std::span<float>(tmp.data(), tmp.size()));
            if (frames == 0)
                break;
            size_t samples = frames * decoder_->channels();
            ring_->write(std::span<const float>(tmp.data(), samples));
            if (need > frames)
                need -= frames;
            else
                break;
        }
    }

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
        if (ev.type == EngineEventType::TrackStarted && cbsCopy.onTrackStarted) {
            cbsCopy.onTrackStarted(ev.trackId);
        } else if (ev.type == EngineEventType::TrackEnded && cbsCopy.onTrackEnded) {
            double pct = 0;
            if (ev.duration > 0) {
                pct = (ev.position / ev.duration) * 100;
                if (pct < 0)
                    pct = 0;
                if (pct > 100)
                    pct = 100;
            }
            cbsCopy.onTrackEnded(ev.trackId, pct);
        } else if (ev.type == EngineEventType::QueueChanged && cbsCopy.onQueueChanged) {
            cbsCopy.onQueueChanged(ev.queueId);
        } else if (ev.type == EngineEventType::Error && cbsCopy.onError) {
            cbsCopy.onError(caudio::utils::Result::Internal, ev.msg);
        }
    }

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
        if (err) {
            sqlite3_free(err);
            err = nullptr;
        }
        if (rc != SQLITE_OK) {
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        // fetch track
        sqlite3_stmt* stmt = nullptr;
        const char* selSql = "SELECT id, play_count FROM tracks WHERE id=?";
        bool ok = true;
        int64_t playCount = 0;
        rc = sqlite3_prepare_v2(h, selSql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            ok = false;
        else {
            sqlite3_bind_int64(stmt, 1, currentTrack_.id);
            if (sqlite3_step(stmt) == SQLITE_ROW)
                playCount = sqlite3_column_int64(stmt, 1);
            else
                ok = false;
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
        if (!ok) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        // update track
        const char* updSql = "UPDATE tracks SET play_count=?, last_played=? WHERE id=?";
        rc = sqlite3_prepare_v2(h, updSql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            ok = false;
        else {
            int64_t nowSec = (int64_t)(detail::nowMs() / 1000);
            sqlite3_bind_int64(stmt, 1, playCount + 1);
            sqlite3_bind_int64(stmt, 2, nowSec);
            sqlite3_bind_int64(stmt, 3, currentTrack_.id);
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE)
                ok = false;
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
        if (!ok) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        // history insert
        const char* insSql = "INSERT INTO history (track_id, started_at, completed_at, "
                             "position_ms, completion_pct, queue_id) VALUES (?,?,?,?,?,?)";
        rc = sqlite3_prepare_v2(h, insSql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            ok = false;
        else {
            int64_t posMs = (int64_t)(pos * 1000.0);
            double compPct = 0;
            if (dur > 0) {
                compPct = (pos / dur) * 100;
                if (compPct > 100)
                    compPct = 100;
            }
            sqlite3_bind_int64(stmt, 1, currentTrack_.id);
            sqlite3_bind_int64(stmt, 2, startedMs_);
            sqlite3_bind_int64(stmt, 3, (int64_t)detail::nowMs());
            sqlite3_bind_int64(stmt, 4, posMs);
            sqlite3_bind_double(stmt, 5, compPct);
            sqlite3_bind_int64(stmt, 6, queue_.queueId ? queue_.queueId : 1);
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE)
                ok = false;
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
        if (!ok) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        rc = sqlite3_exec(h, "COMMIT", nullptr, nullptr, &err);
        if (err)
            sqlite3_free(err);
        if (rc != SQLITE_OK) {
            sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
            markedPlayed_.store(false, std::memory_order_release);
            return;
        }
        // success keep marked true
    }

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
                ev.trackId = hasCurrent_.load(std::memory_order_acquire) ? currentTrack_.id
                                                                         : state_.currentTrackId;
                ev.queueId = queue_.queueId ? queue_.queueId : 1;
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

    void decodeLoop(std::stop_token st) {
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
            // gate preroll: if not enough space, wait
            if (avail < 512) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            std::vector<float> buf(1024 * (decoder_->channels() ? decoder_->channels() : 2));
            size_t frames = decoder_->decode(std::span<float>(buf.data(), buf.size()));
            if (frames == 0) {
                // EOF reached - wait a bit, let monitor handle next (gapless) or stop
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            size_t samples = frames * (decoder_->channels() ? decoder_->channels() : 2);
            size_t written = ring_->write(std::span<const float>(buf.data(), samples));
            (void)written;
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

    std::unique_ptr<caudio::db::Database> db_{};
    std::shared_ptr<caudio::db::Database> dbShared_{};
    bool ownsDb_{false};

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
    std::atomic<bool> gaplessArmed_{false};
    std::atomic<int> queueLock_{0};
};

} // namespace caudio::engine
