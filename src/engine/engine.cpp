#include <sqlite3.h>

#include <atomic>
#include <caudio/db/core.hpp>
#include <caudio/db/types.hpp>
#include <caudio/engine/core.hpp>
#include <caudio/engine/types.hpp>
#include <caudio/player/core.hpp>
#include <caudio/player/decoder.hpp>
#include <caudio/player/output.hpp>
#include <caudio/player/reader.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/log.hpp>
#include <caudio/utils/math.hpp>
#include <caudio/utils/mpsc_queue.hpp>
#include <caudio/utils/result.hpp>
#include <caudio/utils/ring.hpp>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <db/stmt_helpers.hpp>
#include <engine/history.hpp>
#include <engine/shuffle.hpp>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace caudio::engine {

// Transport-command wait budget: device setup under the queue lock routinely
// takes tens of ms (device open + preroll), so a colliding skip waits
// instead of failing Busy. Monitor paths keep try_lock (fail-fast).
namespace {
constexpr std::chrono::milliseconds kQueueLockWait{500};
}

// All engine state and implementation live here so the public header stays
// cheap (no mpsc/ring/std-threading includes). Engine methods delegate.
struct Engine::Impl {
    explicit Impl(const EngineConfig& cfg);

    // lifecycle
    std::optional<caudio::utils::Error> init();
    void shutdown();

    // database / persistence
    bool hasDb() const noexcept;
    ExpectedVoid attachDatabase(std::shared_ptr<caudio::db::Database> db);
    std::optional<caudio::utils::Error> loadState();
    std::expected<void, caudio::utils::Error> saveState();
    std::expected<void, caudio::utils::Error> persistShuffleBlobLocked();
    std::expected<void, caudio::utils::Error> persistCursorLocked();
    sqlite3* dbHandle() const noexcept;
    std::shared_mutex* dbMutex() const noexcept;
    std::expected<void, caudio::utils::Error>
    withTransaction(std::function<std::expected<void, caudio::utils::Error>(struct sqlite3*)> fn);
    std::expected<caudio::types::Track, caudio::utils::Error> fetchTrackByPosLocked(int64_t qid,
                                                                                 int64_t pos);

    // queue locking
    bool tryLockQueue() noexcept;
    bool tryLockQueueFor(std::chrono::milliseconds wait) noexcept;
    void unlockQueue() noexcept;
    std::unexpected<caudio::utils::Error> queueBusy(std::string_view op,
                                                    std::string_view msg) const;

    // transport
    ExpectedVoid play(int64_t queueId);
    ExpectedVoid pause();
    ExpectedVoid resume();
    ExpectedVoid stop();
    ExpectedVoid seek(double seconds);
    ExpectedVoid suspendCurrentTrack();
    ExpectedVoid resumeAfterTagWrite(const caudio::types::Track& t, double pos, bool wasPaused);
    ExpectedVoid next();
    ExpectedVoid autoNext();
    ExpectedVoid prev();
    ExpectedVoid advanceLocked(bool stopAtEnd, int lockWaitMs = 0);
    ExpectedVoid switchQueue(int64_t qid);

    // volume / state accessors
    ExpectedVoid setVolume(float g);
    float volume() const noexcept;
    ExpectedVoid setPreMuteVolume(float g);
    float preMuteVolume() const noexcept;
    caudio::types::PlaybackState state() const noexcept;
    double duration() const noexcept;
    double position() const noexcept;
    int64_t currentTrackId() const noexcept;
    double currentPositionLocked() const noexcept;
    bool shuffle() const noexcept;
    caudio::types::RepeatMode repeat() const noexcept;
    int64_t activeQueueId() const noexcept;

    // shuffle / repeat
    std::vector<int64_t> shufflePermFor(int64_t qid) noexcept;
    void noteEnqueued(int64_t qid, std::size_t count) noexcept;
    ExpectedVoid setShuffle(bool on);
    std::expected<void, caudio::utils::Error> setShuffleLocked(bool on);
    ExpectedVoid setRepeat(caudio::types::RepeatMode m);

    // library / history
    std::expected<caudio::db::DbStats, caudio::utils::Error> getStats();
    std::expected<std::vector<caudio::types::HistoryEntry>, caudio::utils::Error>
    listHistory(int limit);
    std::expected<void, caudio::utils::Error> clearHistory();
    std::string lastError() const;

    // queue traversal
    std::expected<void, caudio::utils::Error> queueNextLocked(caudio::types::Track& out);
    std::expected<void, caudio::utils::Error> queuePrevLocked(caudio::types::Track& out);
    std::expected<void, caudio::utils::Error> doPlayTrack(caudio::types::Track& t);
    void preroll();

    // callbacks / events
    ExpectedVoid setCallbacks(const EngineCallbacks& cbs);
    std::expected<EngineEvent, caudio::utils::Error> pollEvent();
    ExpectedVoid drainEvents(EngineEvent* buf, size_t cap, size_t* n);
    std::vector<EngineEvent> drainAll();

    // monitor / decode
    void engineTick();
    void monitorLoop(std::stop_token st);
    void decodeLoop(std::stop_token st);
    void doHistoryMark();
    void pushEvent(const EngineEvent& ev);

    // members (moved verbatim from Engine)
    EngineConfig cfg_{};
    EngineState state_{};
    QueueState queue_{};
    caudio::types::Track currentTrack_{};
    std::atomic<bool> hasCurrent_{false};
    double duration_{0};
    std::atomic<float> volume_{1.0f};
    std::atomic<float> preMute_{0.5f};
    std::atomic<caudio::types::PlaybackState> playbackState_{caudio::types::PlaybackState::Stopped};
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
    // M-3: volume-drag throttle -- setVolume() persists at most 1x/s
    // (crash-window volume loss <= 1 s; shutdown/track-change saves cover it).
    std::atomic<uint64_t> lastVolSaveMs_{0};
    // gaplessArmed_: 0->1 CAS arms gapless pre-roll ~300ms before track end (gaplessMs).
    // Reset to false on TrackStarted / next() failure. Requires engineTick() single-writer.
    std::atomic<bool> gaplessArmed_{false};
    mutable std::timed_mutex queueMutex_;
};

Engine::Engine(const EngineConfig& cfg) : impl_(std::make_unique<Impl>(cfg)) {}

Engine::ExpectedVoid Engine::attachDatabase(std::shared_ptr<caudio::db::Database> db) {
    return impl_->attachDatabase(std::move(db));
}

void Engine::shutdown() {
    impl_->shutdown();
}

Engine::ExpectedVoid Engine::play(int64_t queueId) {
    return impl_->play(queueId);
}

Engine::ExpectedVoid Engine::pause() {
    return impl_->pause();
}

Engine::ExpectedVoid Engine::resume() {
    return impl_->resume();
}

Engine::ExpectedVoid Engine::stop() {
    return impl_->stop();
}

Engine::ExpectedVoid Engine::seek(double seconds) {
    return impl_->seek(seconds);
}

Engine::ExpectedVoid Engine::suspendCurrentTrack() {
    return impl_->suspendCurrentTrack();
}

Engine::ExpectedVoid Engine::resumeAfterTagWrite(const caudio::types::Track& t, double pos,
                                                 bool wasPaused) {
    return impl_->resumeAfterTagWrite(t, pos, wasPaused);
}

Engine::ExpectedVoid Engine::setVolume(float g) {
    return impl_->setVolume(g);
}

float Engine::volume() const noexcept {
    return impl_->volume();
}

float Engine::preMuteVolume() const noexcept {
    return impl_->preMuteVolume();
}

Engine::ExpectedVoid Engine::setPreMuteVolume(float g) {
    return impl_->setPreMuteVolume(g);
}

caudio::types::PlaybackState Engine::state() const noexcept {
    return impl_->state();
}

double Engine::duration() const noexcept {
    return impl_->duration();
}

double Engine::position() const noexcept {
    return impl_->position();
}

int64_t Engine::currentTrackId() const noexcept {
    return impl_->currentTrackId();
}

bool Engine::shuffle() const noexcept {
    return impl_->shuffle();
}

std::vector<int64_t> Engine::shufflePermFor(int64_t qid) noexcept {
    return impl_->shufflePermFor(qid);
}

void Engine::noteEnqueued(int64_t qid, std::size_t count) noexcept {
    impl_->noteEnqueued(qid, count);
}

caudio::types::RepeatMode Engine::repeat() const noexcept {
    return impl_->repeat();
}

std::expected<caudio::db::DbStats, caudio::utils::Error> Engine::getStats() {
    return impl_->getStats();
}

std::expected<std::vector<caudio::types::HistoryEntry>, caudio::utils::Error>
Engine::listHistory(int limit) {
    return impl_->listHistory(limit);
}

std::expected<void, caudio::utils::Error> Engine::clearHistory() {
    return impl_->clearHistory();
}

std::string Engine::lastError() const {
    return impl_->lastError();
}

Engine::ExpectedVoid Engine::setShuffle(bool on) {
    return impl_->setShuffle(on);
}

Engine::ExpectedVoid Engine::setRepeat(caudio::types::RepeatMode m) {
    return impl_->setRepeat(m);
}

int64_t Engine::activeQueueId() const noexcept {
    return impl_->activeQueueId();
}

Engine::ExpectedVoid Engine::switchQueue(int64_t qid) {
    return impl_->switchQueue(qid);
}

Engine::ExpectedVoid Engine::next() {
    return impl_->next();
}

Engine::ExpectedVoid Engine::autoNext() {
    return impl_->autoNext();
}

Engine::ExpectedVoid Engine::prev() {
    return impl_->prev();
}

Engine::ExpectedVoid Engine::setCallbacks(const EngineCallbacks& cbs) {
    return impl_->setCallbacks(cbs);
}

std::expected<EngineEvent, caudio::utils::Error> Engine::pollEvent() {
    return impl_->pollEvent();
}

Engine::ExpectedVoid Engine::drainEvents(EngineEvent* buf, size_t cap, size_t* n) {
    return impl_->drainEvents(buf, cap, n);
}

std::vector<EngineEvent> Engine::drainAll() {
    return impl_->drainAll();
}

Engine::ExpectedEngine Engine::create(const EngineConfig& cfg) {
    auto e = std::unique_ptr<Engine>(new Engine(cfg));
    auto err = e->impl_->init();
    if (err)
        return std::unexpected(*err);
    if (cfg.logger)
        cfg.logger->debug("engine created monitor={}", cfg.enableMonitorThread);
    return e;
}

Engine::ExpectedEngine Engine::open(std::string_view path, const EngineConfig& cfg) {
    caudio::db::DbOpts dbOpts;
    dbOpts.logger = cfg.logger;
    auto dbRes = caudio::db::Database::open(path, dbOpts);
    if (!dbRes)
        return std::unexpected(dbRes.error());
    auto e = std::unique_ptr<Engine>(new Engine(cfg));
    e->impl_->db_ = std::shared_ptr<caudio::db::Database>(std::move(dbRes.value()));
    if (auto err = e->impl_->init())
        return std::unexpected(*err);
    (void)e->impl_->loadState();
    if (e->impl_->output_)
        e->impl_->output_->setVolume(e->impl_->state_.volume);
    if (cfg.logger)
        cfg.logger->info("engine opened database path={}", path);
    return e;
}

Engine::ExpectedVoid Engine::Impl::attachDatabase(std::shared_ptr<caudio::db::Database> db) {
    if (!db || !db->handle())
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "null db"));
    db_ = std::move(db);
    if (auto ec = loadState())
        return std::unexpected(*ec);
    return {};
}

Engine::~Engine() {
    shutdown();
}

void Engine::Impl::shutdown() {
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

Engine::ExpectedVoid Engine::Impl::play(int64_t queueId) {
    if (!hasDb())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    if (queueId == 0)
        queueId = 1;
    auto s = playbackState_.load(std::memory_order_acquire);
    if (s == caudio::types::PlaybackState::Paused) {
        // resume, don't dequeue
        playStart_ = std::chrono::steady_clock::now();
        playbackState_.store(caudio::types::PlaybackState::Playing, std::memory_order_release);
        if (output_)
            output_->start();
        decodeCv_.notify_all();
        monCv_.notify_all();
        return {};
    }
    if (s == caudio::types::PlaybackState::Playing) {
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
    if (!tryLockQueueFor(kQueueLockWait))
        return queueBusy("play", "queue busy");
    queue_.queue_id = queueId;
    caudio::types::Track t;
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

Engine::ExpectedVoid Engine::Impl::pause() {
    auto s = playbackState_.load(std::memory_order_acquire);
    if (s != caudio::types::PlaybackState::Playing)
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::State, "not playing"));
    // record pause position
    pausePos_ = currentPositionLocked();
    playbackState_.store(caudio::types::PlaybackState::Paused, std::memory_order_release);
    if (output_)
        output_->stop();
    return {};
}

Engine::ExpectedVoid Engine::Impl::resume() {
    auto s = playbackState_.load(std::memory_order_acquire);
    if (s != caudio::types::PlaybackState::Paused)
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::State, "not paused"));
    playStart_ = std::chrono::steady_clock::now();
    playbackState_.store(caudio::types::PlaybackState::Playing, std::memory_order_release);
    if (output_)
        output_->start();
    decodeCv_.notify_all();
    monCv_.notify_all();
    return {};
}

Engine::ExpectedVoid Engine::Impl::stop() {
    auto s = playbackState_.load(std::memory_order_acquire);
    if (s == caudio::types::PlaybackState::Stopped)
        return {};
    playbackState_.store(caudio::types::PlaybackState::Stopped, std::memory_order_release);
    // Release playback resources (decoder/output/ring hold tens of MB for
    // large files; measured +47 MB private retained after stop). Replay
    // re-opens via doPlayTrack. decodeMtx_ serializes with an in-flight
    // decode chunk (queue lock already released above: queue-to-decodeMtx_).
    {
        std::unique_lock<std::mutex> lk(decodeMtx_);
        if (output_) {
            output_->stop();
            output_.reset();
        }
        decoder_.reset();
        reader_.reset();
        ring_.reset();
    }
    pausePos_ = 0;
    hasCurrent_.store(false, std::memory_order_release);
    // Rewind the cursor to the stopped track: cursor addresses the NEXT track,
    // so a later play would otherwise skip the stopped one. Already-stopped
    // returns above, so double-stop cannot rewind twice.
    if (tryLockQueue()) {
        if (queue_.cursor > 0) {
            queue_.cursor--;
            (void)persistCursorLocked();
        }
        unlockQueue();
    }
    return {};
}

Engine::ExpectedVoid Engine::Impl::seek(double seconds) {
    if (!hasCurrent_.load(std::memory_order_acquire))
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::State, "no track"));
    if (!std::isfinite(seconds) || seconds < 0)
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "bad seconds"));

    // Pause decode thread to safely seek + reset ring (prevents race with decodeLoop)
    std::unique_lock<std::mutex> lk(decodeMtx_);
    playbackState_.store(caudio::types::PlaybackState::Paused, std::memory_order_release);
    // snapshot for restore on error (Task 1: seek hardening)
    double savedPause = pausePos_;
    auto savedStart = playStart_;

    if (decoder_) {
        auto res = decoder_->seek(seconds);
        if (!res) {
            pausePos_ = savedPause;
            playStart_ = savedStart;
            playbackState_.store(caudio::types::PlaybackState::Playing, std::memory_order_release);
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
    playbackState_.store(caudio::types::PlaybackState::Playing, std::memory_order_release);
    decodeCv_.notify_all();
    return {};
}

Engine::ExpectedVoid Engine::Impl::suspendCurrentTrack() {
    if (!hasCurrent_.load(std::memory_order_acquire)) {
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::State, "no track"));
    }
    if (!tryLockQueueFor(kQueueLockWait)) {
        return queueBusy("suspend", "queue busy");
    }
    // Mirror stop() teardown but keep cursor, current track, and history
    // mark: the file handles must go so TagLib can rewrite the file, while
    // everything needed to resume stays in place. decodeMtx_ serializes with
    // an in-flight decode chunk (like stop(); queue-to-decodeMtx_ order).
    std::unique_lock<std::mutex> lk(decodeMtx_);
    playbackState_.store(caudio::types::PlaybackState::Paused, std::memory_order_release);
    if (output_)
        output_->stop();
    if (ring_)
        ring_->reset();
    decoder_.reset();
    reader_.reset();
    lk.unlock();
    unlockQueue();
    decodeCv_.notify_all();
    monCv_.notify_all();
    return {};
}

Engine::ExpectedVoid Engine::Impl::resumeAfterTagWrite(const caudio::types::Track& t, double pos,
                                                       bool wasPaused) {
    if (!tryLockQueueFor(kQueueLockWait)) {
        return queueBusy("resume", "queue busy");
    }
    // The user may have moved on while suspended (next/prev/play from
    // another client): the tag/db update already landed, so there is
    // nothing to reopen. Success, not an error.
    if (!hasCurrent_.load(std::memory_order_acquire) || state_.currentTrackId != t.id) {
        unlockQueue();
        return {};
    }
    bool marked = markedPlayed_.load(std::memory_order_acquire);
    caudio::types::Track reopen = t;
    auto r = doPlayTrack(reopen);
    markedPlayed_.store(marked, std::memory_order_release);
    if (!r) {
        unlockQueue();
        return std::unexpected(r.error());
    }
    // doPlayTrack starts at 0/Playing; restore position (seek clamps to
    // duration and ends Playing), then restore the prior paused state.
    auto sr = seek(pos);
    if (!sr) {
        unlockQueue();
        return std::unexpected(sr.error());
    }
    if (wasPaused) {
        auto pr = pause();
        if (!pr) {
            unlockQueue();
            return std::unexpected(pr.error());
        }
    }
    unlockQueue();
    return {};
}

Engine::ExpectedVoid Engine::Impl::setVolume(float g) {
    g = caudio::utils::clampVolume(g);
    state_.volume = g;
    volume_.store(g, std::memory_order_relaxed);
    if (output_)
        output_->setVolume(g);
    if (hasDb()) {
        // Throttle persistence (M-3): a volume drag emits dozens of
        // events/s; each saveState() is a WAL transaction rewriting the
        // shuffle BLOB. Persist at most 1x/s here -- shutdown/track-change
        // paths always persist, so the crash window is <= 1 s of volume.
        const uint64_t now = detail::nowMs();
        const uint64_t last = lastVolSaveMs_.load(std::memory_order_acquire);
        if (now - last >= 1000) {
            lastVolSaveMs_.store(now, std::memory_order_release);
            state_.cursorPos = (int64_t)queue_.cursor;
            (void)saveState();
        }
    }
    return {};
}

float Engine::Impl::volume() const noexcept {
    return volume_.load(std::memory_order_relaxed);
}

Engine::ExpectedVoid Engine::Impl::setPreMuteVolume(float g) {
    if (!(g > 0.0f) || g > 1.0f)
        g = 0.5f;
    state_.preMuteVolume = g;
    preMute_.store(g, std::memory_order_relaxed);
    // Persisted by the setVolume() call that always follows in mute flows.
    return {};
}

float Engine::Impl::preMuteVolume() const noexcept {
    return preMute_.load(std::memory_order_relaxed);
}

caudio::types::PlaybackState Engine::Impl::state() const noexcept {
    return playbackState_.load(std::memory_order_acquire);
}

double Engine::Impl::duration() const noexcept {
    return duration_;
}

double Engine::Impl::position() const noexcept {
    return currentPositionLocked();
}

int64_t Engine::Impl::currentTrackId() const noexcept {
    if (hasCurrent_.load(std::memory_order_acquire))
        return currentTrack_.id;
    return state_.currentTrackId;
}

bool Engine::Impl::shuffle() const noexcept {
    return queue_.shuffle;
}

caudio::types::RepeatMode Engine::Impl::repeat() const noexcept {
    return queue_.repeat;
}

std::vector<int64_t> Engine::Impl::shufflePermFor(int64_t qid) noexcept {
    if (!tryLockQueue())
        return {};
    std::vector<int64_t> out;
    if (queue_.queue_id == qid && queue_.shuffle && !queue_.perm.empty())
        out = queue_.perm;
    unlockQueue();
    return out;
}

void Engine::Impl::noteEnqueued(int64_t qid, std::size_t count) noexcept {
    if (count == 0 || !hasDb() || !tryLockQueue())
        return;
    if (queue_.queue_id == qid && queue_.shuffle && !queue_.perm.empty()) {
        std::size_t n = db_->queueCountLocked(qid);
        std::size_t start = (n >= count) ? n - count : 0;
        for (std::size_t i = start; i < n; ++i)
            queue_.perm.push_back((int64_t)i);
        (void)persistShuffleBlobLocked();
    }
    unlockQueue();
}

std::expected<caudio::db::DbStats, caudio::utils::Error> Engine::Impl::getStats() {
    if (!hasDb())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    return db_->getStats();
}

std::expected<std::vector<caudio::types::HistoryEntry>, caudio::utils::Error>
Engine::Impl::listHistory(int limit) {
    if (!hasDb())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    caudio::engine::History hist(db_);
    return hist.listHistory(limit);
}

std::expected<void, caudio::utils::Error> Engine::Impl::clearHistory() {
    if (!hasDb())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    caudio::engine::History hist(db_);
    return hist.clearHistory();
}

std::string Engine::Impl::lastError() const {
    if (!lastErr_.empty())
        return lastErr_;
    return "";
}

Engine::ExpectedVoid Engine::Impl::setShuffle(bool on) {
    if (!hasDb())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    if (!tryLockQueue())
        return queueBusy("setShuffle", "busy");
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

Engine::ExpectedVoid Engine::Impl::setRepeat(caudio::types::RepeatMode m) {
    if (m != caudio::types::RepeatMode::Off && m != caudio::types::RepeatMode::All && m != caudio::types::RepeatMode::One)
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "bad repeat"));
    if (!tryLockQueue())
        return queueBusy("setRepeat", "busy");
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

int64_t Engine::Impl::activeQueueId() const noexcept {
    std::lock_guard<std::timed_mutex> lk(queueMutex_);
    return queue_.queue_id ? queue_.queue_id : state_.activeQueueId ? state_.activeQueueId : 1;
}

Engine::ExpectedVoid Engine::Impl::switchQueue(int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!hasDb())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    if (!tryLockQueue())
        return queueBusy("switchQueue", "busy");
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

Engine::ExpectedVoid Engine::Impl::next() {
    return advanceLocked(false, (int)kQueueLockWait.count());
}

Engine::ExpectedVoid Engine::Impl::autoNext() {
    return advanceLocked(true);
}

Engine::ExpectedVoid Engine::Impl::advanceLocked(bool stopAtEnd, int lockWaitMs) {
    if (!hasDb())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    // handle repeat one without shuffle without queue lock? match C: if !shuffle && repeat==One
    // && hasCurrent => seek 0 and play - serialize with decodeLoop via decodeMtx_
    if (!queue_.shuffle && queue_.repeat == caudio::types::RepeatMode::One &&
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
        playbackState_.store(caudio::types::PlaybackState::Playing, std::memory_order_release);
        markedPlayed_.store(false, std::memory_order_release);
        gaplessArmed_.store(false, std::memory_order_release);
        startedMs_ = (int64_t)detail::wallMs();
        EngineEvent ev;
        ev.type = EngineEventType::TrackStarted;
        ev.track_id = currentTrack_.id;
        ev.queue_id = queue_.queue_id;
        ev.duration = duration_;
        pushEvent(ev);
        decodeCv_.notify_all();
        return {};
    }
    // Monitor autoNext passes 0 (fail-fast; the next tick retries), user
    // next() passes the wait budget so colliding skips execute.
    bool locked =
        lockWaitMs > 0 ? tryLockQueueFor(std::chrono::milliseconds(lockWaitMs)) : tryLockQueue();
    if (!locked) {
        // Monitor autoNext fail-fast is routine (next tick retries) — stay
        // silent; only user-wait collisions are diagnostic.
        if (lockWaitMs > 0)
            return queueBusy("next", "busy");
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::Busy, "busy"));
    }
    if (stopAtEnd && queue_.repeat == caudio::types::RepeatMode::Off) {
        // Natural track end: halt instead of wrapping. The cursor stays parked
        // past the end, so a later play wraps to the head. Manual next() still
        // wraps (same repeat mode).
        bool atEnd = false;
        if (queue_.shuffle) {
            atEnd = queue_.cursor >= queue_.perm.size();
        } else {
            size_t cnt = db_->queueCountLocked(queue_.queue_id);
            atEnd = queue_.cursor >= cnt;
        }
        if (atEnd) {
            unlockQueue();
            playbackState_.store(caudio::types::PlaybackState::Stopped, std::memory_order_release);
            if (output_)
                output_->stop();
            if (ring_)
                ring_->reset();
            pausePos_ = 0;
            hasCurrent_.store(false, std::memory_order_release);
            return {};
        }
    }
    caudio::types::Track t;
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

Engine::ExpectedVoid Engine::Impl::prev() {
    if (!hasDb())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    if (!tryLockQueueFor(kQueueLockWait))
        return queueBusy("prev", "busy");
    caudio::types::Track t;
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

Engine::ExpectedVoid Engine::Impl::setCallbacks(const EngineCallbacks& cbs) {
    std::lock_guard<std::mutex> lk(cbMutex_);
    callbacks_ = cbs;
    cfg_.callbacks = cbs;
    return {};
}

std::expected<EngineEvent, caudio::utils::Error> Engine::Impl::pollEvent() {
    auto r = eventQueue_.pop();
    if (!r)
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "no event"));
    return r.value();
}

Engine::ExpectedVoid Engine::Impl::drainEvents(EngineEvent* buf, size_t cap, size_t* n) {
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

std::vector<EngineEvent> Engine::Impl::drainAll() {
    std::vector<EngineEvent> out;
    while (true) {
        auto r = eventQueue_.pop();
        if (!r)
            break;
        out.push_back(r.value());
    }
    return out;
}

Engine::Impl::Impl(const EngineConfig& cfg) : cfg_(cfg), state_{}, queue_{} {
    cfg_.pollMs = cfg.pollMs ? cfg.pollMs : 10;
    cfg_.gaplessMs = cfg.gaplessMs ? cfg.gaplessMs : 300;
    cfg_.historyThresholdPct = cfg.historyThresholdPct ? cfg.historyThresholdPct : 60;
    cfg_.historyThresholdSecs = cfg.historyThresholdSecs ? cfg.historyThresholdSecs : 90;
    state_.volume = 1.0f;
    volume_.store(1.0f, std::memory_order_relaxed);
    state_.repeatMode = caudio::types::RepeatMode::Off;
    queue_.repeat = caudio::types::RepeatMode::Off;
    queue_.queue_id = 1;
    playbackState_.store(caudio::types::PlaybackState::Stopped, std::memory_order_release);
    hasCurrent_.store(false, std::memory_order_release);
    markedPlayed_.store(false, std::memory_order_release);
    gaplessArmed_.store(false, std::memory_order_release);
    lastProgressMs_.store(0, std::memory_order_release);
    // callbacks from config
    callbacks_ = cfg.callbacks;
}

std::optional<caudio::utils::Error> Engine::Impl::init() {
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

bool Engine::Impl::hasDb() const noexcept {
    return db_ && db_->handle();
}

bool Engine::Impl::tryLockQueue() noexcept {
    return queueMutex_.try_lock();
}

bool Engine::Impl::tryLockQueueFor(std::chrono::milliseconds wait) noexcept {
    return queueMutex_.try_lock_for(wait);
}

void Engine::Impl::unlockQueue() noexcept {
    queueMutex_.unlock();
}

std::unexpected<caudio::utils::Error> Engine::Impl::queueBusy(std::string_view op,
                                                              std::string_view msg) const {
    if (cfg_.logger)
        cfg_.logger->debug("queue busy op={}", op);
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::StatusCode::Busy, std::string(msg))};
}

double Engine::Impl::currentPositionLocked() const noexcept {
    if (!hasCurrent_.load(std::memory_order_acquire))
        return 0.0;
    auto st = playbackState_.load(std::memory_order_acquire);
    if (st == caudio::types::PlaybackState::Paused)
        return pausePos_;
    if (st != caudio::types::PlaybackState::Playing)
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

sqlite3* Engine::Impl::dbHandle() const noexcept {
    if (db_)
        return db_->handle();
    return nullptr;
}

std::shared_mutex* Engine::Impl::dbMutex() const noexcept {
    if (db_)
        return &db_->mutex();
    return nullptr;
}

std::optional<caudio::utils::Error> Engine::Impl::loadState() {
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
    caudio::db::internal::StmtGuard stmt(raw);
    raw = stmt.get();
    rc = sqlite3_step(raw);
    if (rc == SQLITE_ROW) {
        state_.shuffleEnabled = sqlite3_column_int(raw, 0);
        state_.repeatMode = (caudio::types::RepeatMode)sqlite3_column_int(raw, 1);
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
        // Best-effort pre_mute_volume (missing on old DBs -> keep 0.5 default).
        {
            sqlite3_stmt* pm = nullptr;
            if (sqlite3_prepare_v2(h, "SELECT pre_mute_volume FROM engine_state WHERE id=1", -1,
                                   &pm, nullptr) == SQLITE_OK) {
                caudio::db::internal::StmtGuard pmGuard(pm);
                pm = pmGuard.get();
                if (sqlite3_step(pm) == SQLITE_ROW) {
                    float v = (float)sqlite3_column_double(pm, 0);
                    if (v > 0.0f && v <= 1.0f) {
                        state_.preMuteVolume = v;
                        preMute_.store(v, std::memory_order_relaxed);
                    }
                }
            }
        }
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
        state_.repeatMode = caudio::types::RepeatMode::Off;
        state_.cursorPos = 0;
        state_.currentTrackId = 0;
        state_.volume = 1.0f;
        state_.activeQueueId = 1;
        volume_.store(1.0f, std::memory_order_relaxed);
        queue_.perm.clear();
        queue_.cursor = 0;
        queue_.shuffle = false;
        queue_.repeat = caudio::types::RepeatMode::Off;
        queue_.queue_id = 1;
        return std::nullopt;
    }
    return caudio::utils::makeError(caudio::utils::StatusCode::Internal, "load failed");
}

std::expected<void, caudio::utils::Error> Engine::Impl::withTransaction(
    std::function<std::expected<void, caudio::utils::Error>(struct sqlite3*)> fn) {
    auto* h = dbHandle();
    auto* m = dbMutex();
    if (!h || !m)
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "no db"));
    std::unique_lock<std::shared_mutex> lk(*m);
    char* err = nullptr;
    int rc = sqlite3_exec(h, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
    caudio::db::internal::SqliteErrGuard errGuard{err};
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
    caudio::db::internal::SqliteErrGuard commitGuard{commitErr};
    if (rc != SQLITE_OK) {
        sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "commit failed"));
    }
    return {};
}

std::expected<void, caudio::utils::Error> Engine::Impl::saveState() {
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
        caudio::db::internal::StmtGuard stmt(raw);
        raw = stmt.get();
        sqlite3_bind_int(raw, 1, state_.shuffleEnabled);
        sqlite3_bind_int(raw, 2, (int)state_.repeatMode);
        if (queue_.shuffle && !queue_.perm.empty()) {
            if (queue_.perm.size() >
                (size_t)(std::numeric_limits<int>::max() / (int)sizeof(int64_t))) {
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::NoMem, "perm too large"));
            }
            sqlite3_bind_blob(raw, 3, queue_.perm.data(),
                              (int)(queue_.perm.size() * sizeof(int64_t)), SQLITE_TRANSIENT);
        } else
            sqlite3_bind_null(raw, 3);
        sqlite3_bind_int64(raw, 4, state_.cursorPos);
        sqlite3_bind_int64(raw, 5, state_.currentTrackId);
        sqlite3_bind_double(raw, 6, (double)state_.volume);
        if (hasActiveCol) {
            sqlite3_bind_int64(raw, 7,
                               state_.activeQueueId ? state_.activeQueueId : queue_.queue_id);
        }
        rc = sqlite3_step(raw);
        if (rc != SQLITE_DONE)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "step failed"));
        // Best-effort pre_mute_volume (no-op on old DBs without the column).
        {
            sqlite3_stmt* pm = nullptr;
            if (sqlite3_prepare_v2(db, "UPDATE engine_state SET pre_mute_volume=? WHERE id=1", -1,
                                   &pm, nullptr) == SQLITE_OK) {
                caudio::db::internal::StmtGuard pmGuard(pm);
                pm = pmGuard.get();
                sqlite3_bind_double(pm, 1, (double)state_.preMuteVolume);
                (void)sqlite3_step(pm);
            }
        }
        return {};
    });
}

std::expected<void, caudio::utils::Error> Engine::Impl::persistShuffleBlobLocked() {
    return withTransaction([&](sqlite3* db) -> std::expected<void, caudio::utils::Error> {
        const char* sql = "UPDATE engine_state SET shuffle_perm=?, cursor_pos=?, "
                          "shuffle_enabled=? WHERE id=1";
        sqlite3_stmt* raw = nullptr;
        int rc = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
        caudio::db::internal::StmtGuard stmt(raw);
        if (rc != SQLITE_OK)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "prepare failed"));
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

std::expected<void, caudio::utils::Error> Engine::Impl::persistCursorLocked() {
    return withTransaction([&](sqlite3* db) -> std::expected<void, caudio::utils::Error> {
        const char* sql = "UPDATE engine_state SET cursor_pos=? WHERE id=1";
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "prepare failed"));
        caudio::db::internal::StmtGuard stmt(raw);
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

std::expected<caudio::types::Track, caudio::utils::Error>
Engine::Impl::fetchTrackByPosLocked(int64_t qid, int64_t pos) {
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
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "prepare failed"));
        caudio::db::internal::StmtGuard stmt(raw);
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

std::expected<void, caudio::utils::Error> Engine::Impl::setShuffleLocked(bool on) {
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

std::expected<void, caudio::utils::Error> Engine::Impl::queueNextLocked(caudio::types::Track& out) {
    if (queue_.queue_id == 0)
        queue_.queue_id = 1;
    if (queue_.shuffle) {
        if (queue_.perm.empty()) {
            size_t cnt = db_->queueCountLocked(queue_.queue_id);
            if (cnt == 0)
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "empty queue"));
            auto sr = setShuffleLocked(true);
            if (!sr)
                return std::unexpected(sr.error());
            if (queue_.perm.empty())
                return std::unexpected(
                    caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no perm"));
        }
        if (queue_.cursor >= queue_.perm.size()) {
            if (queue_.repeat == caudio::types::RepeatMode::One) {
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
        if (queue_.repeat == caudio::types::RepeatMode::One) {
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

std::expected<void, caudio::utils::Error> Engine::Impl::queuePrevLocked(caudio::types::Track& out) {
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

std::expected<void, caudio::utils::Error> Engine::Impl::doPlayTrack(caudio::types::Track& t) {
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

    // attempt to open reader & decoder; without allowSimulatedPlayback an
    // unplayable track is an error, never silent timer playback
    bool opened = false;
    if (!path.empty()) {
        auto rRes = caudio::player::FileReader::open(path);
        if (rRes) {
            reader_ = std::move(rRes.value());
            auto dRes = caudio::player::Decoder::open(*reader_, cfg_.logger);
            if (dRes) {
                decoder_ = std::move(dRes.value());
                opened = true;
                duration_ = decoder_->totalFrames()
                                ? (double)decoder_->totalFrames() / (double)decoder_->sampleRate()
                                : t.duration;
                if (duration_ <= 0)
                    duration_ = t.duration > 0 ? t.duration : 1.0;
                // create ring and output
                // NOTE (2026-10-05): kept at 8192*ch -- a flat 8192-frame cap
                // measured +9 MB daemon RSS on an mp3 with an attached mjpeg
                // cover (release-lto-clang, 67.7 -> 76.6 MB WS). Mechanism
                // unknown (suspect indirect FFmpeg buffering/timing effect);
                // do NOT re-shrink without a measured explanation.
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
                cfg.logger = cfg_.logger;
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
        if (!cfg_.allowSimulatedPlayback) {
            const std::string msg =
                lastErr_.empty() ? "track file not playable: " + path : lastErr_;
            if (cfg_.logger)
                cfg_.logger->warn("track not playable file={} err={}", path, msg);
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::NotFound, msg)};
        }
        // timer-only fallback to track metadata duration (no audio output)
        if (cfg_.logger)
            cfg_.logger->warn("simulated playback (no audio) file={}", path);
        duration_ = t.duration > 0 ? t.duration : 1.0;
        // no ring/output needed for simulated playback
    } else if (!output_) {
        // decoder opened but no audio device (Player::openReader fails here
        // with Device); timer fallback only when explicitly opted in
        if (!cfg_.allowSimulatedPlayback) {
            decoder_.reset();
            reader_.reset();
            ring_.reset();
            const std::string msg = lastErr_.empty() ? "audio device unavailable" : lastErr_;
            if (cfg_.logger)
                cfg_.logger->warn("audio device unavailable file={} err={}", path, msg);
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Device, msg)};
        }
    }
    currentTrack_ = t;
    // update state
    hasCurrent_.store(true, std::memory_order_release);
    markedPlayed_.store(false, std::memory_order_release);
    gaplessArmed_.store(false, std::memory_order_release);
    startedMs_ = (int64_t)detail::wallMs();
    state_.currentTrackId = t.id;
    state_.cursorPos = (int64_t)queue_.cursor;
    playbackState_.store(caudio::types::PlaybackState::Playing, std::memory_order_release);
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
    if (cfg_.logger)
        cfg_.logger->debug("playing track={} file={}", t.id, path);
    return {};
}

void Engine::Impl::preroll() {
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

void Engine::Impl::pushEvent(const EngineEvent& ev) {
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

void Engine::Impl::doHistoryMark() {
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
    if (!detail::shouldMarkPlayed(dur, pos, false, pct, secs))
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
    caudio::db::internal::SqliteErrGuard errGuard{err};
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
        caudio::db::internal::StmtGuard guard(raw);
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
        caudio::db::internal::StmtGuard guard(raw);
        if (rc != SQLITE_OK)
            ok = false;
        else {
            raw = guard.get();
            // Wall-clock milliseconds (display divides by 1000 for dates).
            int64_t wallNow = (int64_t)detail::wallMs();
            sqlite3_bind_int64(raw, 1, playCount + 1);
            sqlite3_bind_int64(raw, 2, wallNow);
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
        caudio::db::internal::StmtGuard guard(raw);
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
            sqlite3_bind_int64(raw, 3, (int64_t)detail::wallMs());
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
    caudio::db::internal::SqliteErrGuard commitGuard{commitErr};
    if (rc != SQLITE_OK) {
        sqlite3_exec(h, "ROLLBACK", nullptr, nullptr, nullptr);
        markedPlayed_.store(false, std::memory_order_release);
        return;
    }
    // success keep marked true
}

void Engine::Impl::engineTick() {
    doHistoryMark();
    auto st = playbackState_.load(std::memory_order_acquire);
    if (st == caudio::types::PlaybackState::Playing) {
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
                    // gaplessArmed 0->1 CAS, 300ms preroll
                    bool expected = false;
                    if (gaplessArmed_.compare_exchange_strong(
                            expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
                        auto nr = autoNext();
                        if (!nr)
                            gaplessArmed_.store(false, std::memory_order_release);
                    }
                }
            }
        }
    }
}

void Engine::Impl::monitorLoop(std::stop_token st) {
    // Two-tier waits (C-1): the 10 ms cadence is only needed while playing
    // (300 ms gapless pre-roll window still gets ~12 looks at 25 ms).
    // Idle backs off to 100 ms. An explicit EngineConfig::pollMs override is
    // honored for the playing interval; idle is always >= 100 ms.
    int cfgPoll = cfg_.pollMs ? cfg_.pollMs : 10;
    if (cfgPoll <= 0)
        cfgPoll = 10;
    const bool customPoll = (cfgPoll != 10);
    std::unique_lock<std::mutex> lk(monMtx_);
    while (!st.stop_requested() && monRun_.load(std::memory_order_acquire)) {
        lk.unlock();
        engineTick();
        int poll;
        if (playbackState_.load(std::memory_order_acquire) == caudio::types::PlaybackState::Playing)
            poll = customPoll ? cfgPoll : 25;
        else
            poll = customPoll ? (cfgPoll > 100 ? cfgPoll : 100) : 100;
        lk.lock();
        monCv_.wait_for(lk, std::chrono::milliseconds(poll), [&st, this] {
            return st.stop_requested() || !monRun_.load(std::memory_order_acquire);
        });
    }
}

void Engine::Impl::decodeLoop(std::stop_token st) {
    constexpr std::size_t kMaxChunkFrames = 1024;
    // Reusable decode scratch (P-2): grown only, never reallocated per chunk.
    std::vector<float> scratch;
    scratch.reserve(kMaxChunkFrames * 8);
    while (!st.stop_requested() && decodeRun_.load(std::memory_order_acquire)) {
        auto ps = playbackState_.load(std::memory_order_acquire);
        if (ps != caudio::types::PlaybackState::Playing || !decoder_ || !ring_) {
            std::unique_lock<std::mutex> lk(decodeMtx_);
            // Idle backoff (C-1): woken immediately by decodeCv_ on
            // play/seek/resume, so the longer timeout costs no latency.
            decodeCv_.wait_for(lk, std::chrono::milliseconds(50), [&st, this] {
                return st.stop_requested() ||
                       playbackState_.load(std::memory_order_acquire) == caudio::types::PlaybackState::Playing;
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

        if (scratch.size() < maxFrames * ch)
            scratch.resize(maxFrames * ch);
        std::span<float> chunk(scratch.data(), maxFrames * ch);
        size_t frames;
        {
            std::unique_lock<std::mutex> lk(decodeMtx_);
            // Re-check state under lock to avoid race with seek()
            if (playbackState_.load(std::memory_order_acquire) != caudio::types::PlaybackState::Playing)
                continue;
            frames = decoder_->decode(chunk);
        }
        if (frames == 0) {
            // EOF reached - wait a bit, let monitor handle next (gapless) or stop
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }
        size_t samples = frames * ch;
        size_t writtenFrames;
        {
            std::unique_lock<std::mutex> lk(decodeMtx_);
            // Re-check state under lock to avoid race with seek() ring reset
            if (playbackState_.load(std::memory_order_acquire) != caudio::types::PlaybackState::Playing)
                continue;
            writtenFrames = ring_->write(std::span<float>(scratch.data(), samples));
        }
        if (writtenFrames < frames) {
            // Ring full, will retry next iteration
        }
    }
}

} // namespace caudio::engine
