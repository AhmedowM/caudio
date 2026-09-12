module;
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

export module caudio.player:player_core;

import caudio.utils;
import :reader;
import :decoder;
import :decoder_interface;
import :output;

export namespace caudio::player {

enum class State : uint8_t { Stopped = 0, Playing = 1, Paused = 2, Error = 3 };

struct PlayerOpts {
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;
};

class Player {
  public:
    using ExpectedVoid = std::expected<void, caudio::utils::Error>;
    using ExpectedPlayer = std::expected<std::unique_ptr<Player>, caudio::utils::Error>;

    static ExpectedPlayer create(const PlayerOpts& opts = {}) {
        auto p = std::unique_ptr<Player>(new Player());
        if (!p->init(opts)) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::Device, "player init failed"});
        }
        return std::unique_ptr<Player>(std::move(p));
    }

    ~Player() {
        (void)stop();
        if (decodeThread_.joinable()) {
            decodeThread_.request_stop();
            cv_.notify_all();
            decodeThread_.join();
        }
    }

    ExpectedVoid open(std::string_view path) {
        if (path.empty()) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, "empty path"});
        }
        auto readerResult = FileReader::open(path);
        if (!readerResult) {
            lastError_ = readerResult.error().message;
            state_.store(State::Error, std::memory_order_release);
            return std::unexpected(readerResult.error());
        }
        return openReader(std::move(readerResult.value()));
    }

    ExpectedVoid openReader(std::unique_ptr<Reader> reader) {
        if (!reader) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, "null reader"});
        }

        std::lock_guard<std::mutex> lk(openMutex_);

        // Stop any existing playback
        if (state_.load(std::memory_order_acquire) != State::Stopped) {
            (void)stopInternal();
        }

        // Open decoder
        auto decResult = DecoderRegistry::open(*reader);
        if (!decResult) {
            lastError_ = decResult.error().message;
            state_.store(State::Error, std::memory_order_release);
            return std::unexpected(decResult.error());
        }

        decoder_ = std::move(decResult.value());
        reader_ = std::move(reader);

        // Create ring buffer: capacity = 8192 * channels (like C ca_player.c:462)
        uint32_t sr = decoder_->sampleRate();
        uint32_t ch = decoder_->channels();
        if (sr == 0)
            sr = 48000;
        if (ch == 0)
            ch = 2;

        ring_ = std::make_unique<caudio::utils::SpscRing<float>>(8192 * ch, ch);

        // Create audio output
        AudioOutput::Config cfg;
        cfg.sampleRate = sr;
        cfg.channels = ch;
        cfg.ring = ring_.get();
        cfg.volume = volume_.load(std::memory_order_relaxed);

        auto outResult = AudioOutput::create(cfg);
        if (!outResult) {
            lastError_ = outResult.error().message;
            state_.store(State::Error, std::memory_order_release);
            return std::unexpected(outResult.error());
        }

        output_ = std::move(outResult.value());

        // Preroll: decode cap/2 samples before play returns
        preroll();

        posBase_.store(0, std::memory_order_relaxed);
        posStartMs_.store(0, std::memory_order_relaxed);
        isPlaying_.store(false, std::memory_order_relaxed);
        decodeBusy_.store(false, std::memory_order_relaxed);
        openGate_.store(true, std::memory_order_release);
        lastError_.clear();
        state_.store(State::Stopped, std::memory_order_release);

        return {};
    }

    ExpectedVoid play() {
        if (!decoder_ || !output_ || !ring_) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::State, "not opened"});
        }

        State expected = State::Stopped;
        if (!state_.compare_exchange_strong(expected, State::Playing, std::memory_order_acq_rel)) {
            expected = State::Paused;
            if (!state_.compare_exchange_strong(expected, State::Playing,
                                                std::memory_order_acq_rel)) {
                return std::unexpected(
                    caudio::utils::Error{caudio::utils::StatusCode::State, "already playing"});
            }
        }

        isPlaying_.store(true, std::memory_order_release);
        posStartMs_.store(nowMs(), std::memory_order_release);

        // Start audio device
        output_->start();

        // Wake decode thread
        {
            std::lock_guard<std::mutex> lk(cvMutex_);
            decodeBusy_.store(true, std::memory_order_release);
        }
        cv_.notify_all();

        return {};
    }

    ExpectedVoid pause() {
        State expected = State::Playing;
        if (!state_.compare_exchange_strong(expected, State::Paused, std::memory_order_acq_rel)) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::State, "not playing"});
        }

        isPlaying_.store(false, std::memory_order_release);
        output_->stop();

        // Update posBase with elapsed time
        int64_t now = nowMs();
        int64_t start = posStartMs_.load(std::memory_order_acquire);
        if (start > 0 && now > start) {
            int64_t elapsedFrames = static_cast<int64_t>((now - start) * sampleRate_ / 1000.0);
            int64_t currentBase = posBase_.load(std::memory_order_relaxed);
            posBase_.store(currentBase + elapsedFrames, std::memory_order_relaxed);
        }
        posStartMs_.store(0, std::memory_order_relaxed);

        return {};
    }

    ExpectedVoid resume() {
        State expected = State::Paused;
        if (!state_.compare_exchange_strong(expected, State::Playing, std::memory_order_acq_rel)) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::State, "not paused"});
        }

        isPlaying_.store(true, std::memory_order_release);
        posStartMs_.store(nowMs(), std::memory_order_release);
        output_->start();

        {
            std::lock_guard<std::mutex> lk(cvMutex_);
            decodeBusy_.store(true, std::memory_order_release);
        }
        cv_.notify_all();

        return {};
    }

    ExpectedVoid stop() {
        std::lock_guard<std::mutex> lk(openMutex_);
        return stopInternal();
    }

    ExpectedVoid seek(double seconds) {
        if (!decoder_ || !ring_) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::State, "not opened"});
        }
        if (seconds < 0.0 || std::isnan(seconds) || std::isinf(seconds)) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, "bad seconds"});
        }

        auto seekRes = decoder_->seek(seconds);
        if (!seekRes) {
            lastError_ = seekRes.error().message;
            state_.store(State::Error, std::memory_order_release);
            return std::unexpected(seekRes.error());
        }

        uint64_t newPosBase = static_cast<uint64_t>(seconds * sampleRate_);
        posBase_.store(newPosBase, std::memory_order_relaxed);
        ring_->reset();

        // If playing, update posStartMs and restart decode thread
        State s = state_.load(std::memory_order_acquire);
        if (s == State::Playing) {
            posStartMs_.store(nowMs(), std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(cvMutex_);
                decodeBusy_.store(true, std::memory_order_release);
            }
            cv_.notify_all();
        } else if (s == State::Paused) {
            posStartMs_.store(0, std::memory_order_relaxed);
        }

        return {};
    }

    static inline float clampVolume(float v) noexcept {
        if (!std::isfinite(v))
            return 0.0f;
        return std::clamp(v, 0.0f, 1.0f);
    }

    ExpectedVoid setVolume(float volume) {
        if (std::isnan(volume) || std::isinf(volume)) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, "bad volume"});
        }
        float vol = clampVolume(volume);
        volume_.store(vol, std::memory_order_relaxed);
        if (output_) {
            output_->setVolume(vol);
        }
        return {};
    }

    State state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    std::chrono::duration<double> position() const noexcept {
        State s = state_.load(std::memory_order_acquire);
        if (s == State::Stopped || s == State::Error) {
            return std::chrono::duration<double>(0.0);
        }
        uint64_t base = posBase_.load(std::memory_order_relaxed);
        int64_t start = posStartMs_.load(std::memory_order_acquire);
        if (s == State::Playing && start > 0) {
            int64_t now = nowMs();
            if (now > start) {
                double elapsedSec = static_cast<double>(now - start) / 1000.0;
                uint64_t elapsedFrames = static_cast<uint64_t>(elapsedSec * sampleRate_);
                return std::chrono::duration<double>(static_cast<double>(base + elapsedFrames) /
                                                     sampleRate_);
            }
        }
        return std::chrono::duration<double>(static_cast<double>(base) / sampleRate_);
    }

    std::string_view lastError() const noexcept {
        return lastError_;
    }

  private:
    Player() = default;

    bool init(const PlayerOpts& opts) {
        sampleRate_ = opts.sampleRate ? opts.sampleRate : 48000;
        channels_ = opts.channels ? opts.channels : 2;
        volume_.store(1.0f, std::memory_order_relaxed);
        state_.store(State::Stopped, std::memory_order_relaxed);

        // Start decode thread
        decodeThread_ = std::jthread([this](std::stop_token st) { decodeLoop(st); });

        return true;
    }

    ExpectedVoid stopInternal() {
        State s = state_.load(std::memory_order_acquire);
        if (s == State::Stopped && !decoder_) {
            return {};
        }

        isPlaying_.store(false, std::memory_order_release);
        if (output_) {
            output_->stop();
        }
        if (ring_) {
            ring_->reset();
        }
        posBase_.store(0, std::memory_order_relaxed);
        posStartMs_.store(0, std::memory_order_relaxed);
        state_.store(State::Stopped, std::memory_order_release);
        openGate_.store(false, std::memory_order_release);

        // Wake decode thread to exit cleanly
        cv_.notify_all();

        return {};
    }

    void preroll() {
        if (!decoder_ || !ring_)
            return;

        // Preroll cap/2 samples (C ca_player.c:462)
        std::size_t ringCap = ring_->capacity();
        std::size_t prerollSamples = ringCap / 2;
        uint32_t ch = decoder_->channels();
        std::size_t prerollFrames = prerollSamples / ch;

        // Use fixed chunk size: min(avail, 1024, 2048/ch) like C ca_player.c:156
        std::size_t chunkFrames = 1024;
        std::size_t maxChunk = 2048 / ch;
        if (chunkFrames > maxChunk)
            chunkFrames = maxChunk;

        std::vector<float> buffer(chunkFrames * ch);
        std::size_t filled = 0;

        while (filled < prerollFrames && ring_->availableWrite() >= chunkFrames * ch) {
            std::size_t frames = decoder_->decode(std::span<float>(buffer.data(), buffer.size()));
            if (frames == 0)
                break;
            std::size_t samples = frames * ch;
            ring_->write(std::span<float>(buffer.data(), samples));
            filled += frames;
        }
    }

    void decodeLoop(std::stop_token st) {
        constexpr std::size_t kMaxChunkFrames = 1024;

        while (!st.stop_requested()) {
            // Wait for openGate and decodeBusy
            {
                std::unique_lock<std::mutex> lk(cvMutex_);
                cv_.wait(lk, [this, &st]() {
                    return st.stop_requested() || (openGate_.load(std::memory_order_acquire) &&
                                                   decodeBusy_.load(std::memory_order_acquire));
                });
                if (st.stop_requested())
                    break;
            }

            if (!decoder_ || !ring_ || !output_)
                continue;

            // Check if still playing
            if (!isPlaying_.load(std::memory_order_acquire)) {
                decodeBusy_.store(false, std::memory_order_release);
                continue;
            }

            // Check available space in ring
            std::size_t avail = ring_->availableWrite();
            if (avail == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            uint32_t ch = decoder_->channels();
            std::size_t maxFrames = avail / ch;
            if (maxFrames > kMaxChunkFrames)
                maxFrames = kMaxChunkFrames;
            std::size_t maxChunkByCh = 2048 / ch;
            if (maxFrames > maxChunkByCh)
                maxFrames = maxChunkByCh;
            if (maxFrames == 0)
                maxFrames = 1;

            std::vector<float> buffer(maxFrames * ch);
            std::size_t frames = decoder_->decode(std::span<float>(buffer.data(), buffer.size()));

            if (frames == 0) {
                // EOF reached
                isPlaying_.store(false, std::memory_order_release);
                output_->stop();
                state_.store(State::Stopped, std::memory_order_release);
                decodeBusy_.store(false, std::memory_order_release);
                continue;
            }

            std::size_t samples = frames * ch;
            std::size_t writtenFrames = ring_->write(std::span<float>(buffer.data(), samples));
            if (writtenFrames < frames) {
                // Ring full, will retry next iteration
            }
        }
    }

    static int64_t nowMs() noexcept {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    // Configuration
    uint32_t sampleRate_{48000};
    uint32_t channels_{2};

    // Playback components
    std::unique_ptr<Reader> reader_;
    std::unique_ptr<IDecoder> decoder_;
    std::unique_ptr<caudio::utils::SpscRing<float>> ring_;
    std::unique_ptr<AudioOutput> output_;

    // Threading
    std::jthread decodeThread_;
    std::mutex cvMutex_;
    std::condition_variable cv_;
    std::mutex openMutex_;

    // Atomic state (C ca_player.c atomic fields)
    std::atomic<State> state_{State::Stopped};
    std::atomic<uint64_t> posBase_{0};
    std::atomic<int64_t> posStartMs_{0};
    std::atomic<bool> isPlaying_{false};
    std::atomic<bool> openGate_{false};
    std::atomic<bool> decodeBusy_{false};
    std::atomic<float> volume_{1.0f};

    std::string lastError_;
};

} // namespace caudio::player
