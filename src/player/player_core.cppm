/**
 * @file player_core.cppm
 * @brief Core audio player implementation with gapless playback support
 * @ingroup caudio_player
 *
 * This module provides the Player class, the central component for audio playback.
 * It coordinates decoding, audio output, and playback state management.
 *
 * Architecture:
 * - Decode thread: Runs FfmpegDecoder::decode() to fill SPSC ring buffer
 * - Audio thread (miniaudio): Consumes from ring buffer via AudioOutput::dataCallback
 * - Control thread: Application calls play/pause/stop/seek/volume
 *
 * Thread Safety:
 * - Public methods: Thread-safe (atomic state + mutexes for complex operations)
 * - decodeLoop(): Runs on dedicated jthread, single-threaded
 * - AudioOutput::dataCallback(): Runs on miniaudio audio thread (real-time)
 * - State transitions use compare_exchange for lock-free control
 *
 * Playback State Machine:
 *   Stopped <-> Playing <-> Paused
 *     ^         |          |
 *     |         v          v
 *     +-----> Error <------+
 *
 * Gapless Playback:
 * - Ring buffer prerolls with cap/2 samples before play() returns
 * - Seeking resets ring buffer and updates position atomically
 * - Decode thread maintains steady fill rate; audio thread consumes
 * - No allocations in hot path (decode loop or audio callback)
 *
 * FFmpeg Integration:
 * - DecoderRegistry probes format and creates FfmpegDecoder
 * - FfmpegDecoder uses custom AVIO callbacks for Reader abstraction
 * - SwrContext resamples to float32 interleaved for AudioOutput
 * - Seeking uses stream time_base for accurate container positioning
 */
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

/**
 * @enum State
 * @brief Playback state enumeration
 * @ingroup caudio_player
 *
 * Represents the current state of the Player. Transitions:
 * - Stopped <-> Playing (play/stop)
 * - Playing <-> Paused (pause/resume)
 * - Any -> Error (on decoder/output failure)
 */
enum class State : uint8_t { Stopped = 0, Playing = 1, Paused = 2, Error = 3 };

/**
 * @struct PlayerOpts
 * @brief Player configuration options
 * @ingroup caudio_player
 */
struct PlayerOpts {
    /// Output sample rate in Hz (default: 48000)
    uint32_t sampleRate = 48000;
    /// Output channel count (default: 2)
    uint32_t channels = 2;
};

/**
 * @class Player
 * @brief Core audio player with gapless playback and multi-format support
 * @ingroup caudio_player
 *
 * High-level playback controller coordinating:
 * - Reader abstraction (FileReader, MemoryReader)
 * - Decoder registry (FFmpeg for all common formats)
 * - Lock-free SPSC ring buffer for decode/audio thread communication
 * - miniaudio-based AudioOutput for device playback
 *
 * Thread Model:
 * - Control thread: Application calls open/play/pause/stop/seek/volume
 * - Decode thread: Dedicated jthread running decodeLoop()
 * - Audio thread: miniaudio callback (real-time priority)
 *
 * State Transitions (atomic, lock-free where possible):
 *   create() -> init() -> open() -> play() -> pause()/resume() -> stop()
 *                                    \-> seek() (any state)
 *
 * Gapless Implementation:
 * - Preroll fills ring buffer to cap/2 before play() returns
 * - Seek resets ring, updates posBase atomically, restarts decode
 * - Position tracking uses posBase + elapsed time (monotonic clock)
 *
 * Error Handling:
 * - Errors stored in lastError_, state set to Error
 * - All public methods return Expected<void> for explicit error handling
 */
class Player {
  public:
    using ExpectedVoid = std::expected<void, caudio::utils::Error>;
    using ExpectedPlayer = std::expected<std::unique_ptr<Player>, caudio::utils::Error>;

    /**
     * @brief Create a new Player instance
     * @param opts Configuration options (sample rate, channels)
     * @return Expected containing unique_ptr<Player> on success, Error on failure
     *
     * Factory method that constructs a Player and initializes the decode thread.
     * The player starts in Stopped state; call open() before play().
     *
     * @retval StatusCode::Device Decode thread creation failed
     *
     * Thread Safety: Thread-safe. Can be called from any thread.
     */
    static ExpectedPlayer create(const PlayerOpts& opts = {}) {
        auto p = std::unique_ptr<Player>(new Player());
        if (!p->init(opts)) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::StatusCode::Device, "player init failed"});
        }
        return std::unique_ptr<Player>(std::move(p));
    }

    /**
     * @brief Destructor - stops playback and joins decode thread
     *
     * Stops any active playback, signals the decode thread to stop,
     * and waits for it to join. Safe to call multiple times.
     *
     * Thread Safety: Thread-safe (called from control thread)
     */
    ~Player() {
        (void)stop();
        if (decodeThread_.joinable()) {
            decodeThread_.request_stop();
            cv_.notify_all();
            decodeThread_.join();
        }
    }

    /**
     * @brief Open an audio file for playback
     * @param path Path to the audio file
     * @return void on success, Error on failure
     *
     * Opens the file using FileReader, probes the format via DecoderRegistry,
     * creates the decoder, ring buffer, and audio output. Performs preroll
     * to fill the ring buffer to cap/2 before returning.
     *
     * Supported formats (via FFmpeg): OGG/Vorbis, FLAC, MP3, WAV, M4A/AAC, Opus, WMA
     *
     * @retval StatusCode::InvalidArg Empty path
     * @retval StatusCode::NotFound File not found or cannot be opened
     * @retval StatusCode::Unsupported No decoder recognized the format
     * @retval StatusCode::Io FFmpeg initialization failed
     * @retval StatusCode::Device Audio output device creation failed
     *
     * Thread Safety: Thread-safe (uses openMutex_ for exclusive access)
     * Precondition: Player must be in Stopped state (automatically stops if playing)
     */
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

    /**
     * @brief Open a reader for playback (advanced usage)
     * @param reader Unique pointer to a Reader implementation
     * @return void on success, Error on failure
     *
     * Opens playback from a custom Reader (FileReader, MemoryReader, or custom).
     * Used for non-file sources like network streams or embedded audio data.
     * Stops any existing playback before opening the new reader.
     *
     * @retval StatusCode::InvalidArg Null reader
     * @retval StatusCode::Unsupported No decoder recognized the format
     * @retval StatusCode::Io FFmpeg initialization failed
     * @retval StatusCode::Device Audio output device creation failed
     *
     * Thread Safety: Thread-safe (uses openMutex_ for exclusive access)
     */
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

    /**
     * @brief Start or resume playback
     * @return void on success, Error on failure
     *
     * Transitions state: Stopped -> Playing or Paused -> Playing.
     * Starts the audio device and signals the decode thread to begin filling
     * the ring buffer. Preroll was already performed during open().
     *
     * @retval StatusCode::State Not opened, or already playing
     *
     * Thread Safety: Thread-safe (atomic compare_exchange for state transition)
     */
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

    /**
     * @brief Pause playback
     * @return void on success, Error on failure
     *
     * Transitions state: Playing -> Paused.
     * Stops the audio device and updates position tracking with elapsed time.
     * Decode thread will stop filling the ring buffer.
     *
     * @retval StatusCode::State Not currently playing
     *
     * Thread Safety: Thread-safe (atomic compare_exchange for state transition)
     */
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

    /**
     * @brief Resume playback from paused state
     * @return void on success, Error on failure
     *
     * Transitions state: Paused -> Playing.
     * Restarts the audio device and signals the decode thread to continue.
     * Position tracking resumes from the paused position.
     *
     * @retval StatusCode::State Not currently paused
     *
     * Thread Safety: Thread-safe (atomic compare_exchange for state transition)
     */
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

    /**
     * @brief Stop playback and reset to initial state
     * @return void on success, Error on failure
     *
     * Transitions state: Any -> Stopped.
     * Stops audio device, resets ring buffer, clears position tracking,
     * and closes the open gate to stop the decode thread.
     * Does not close the decoder/reader; call open() again to play.
     *
     * Thread Safety: Thread-safe (uses openMutex_ for exclusive access)
     */
    ExpectedVoid stop() {
        std::lock_guard<std::mutex> lk(openMutex_);
        return stopInternal();
    }

    /**
     * @brief Seek to a specific time position
     * @param seconds Target time in seconds from stream start (must be >= 0, finite)
     * @return void on success, Error on failure
     *
     * Seeks the decoder to the specified position, resets the ring buffer,
     * and updates position tracking atomically. If playing, restarts the
     * decode thread from the new position. If paused, updates position only.
     *
     * @retval StatusCode::State Not opened
     * @retval StatusCode::InvalidArg seconds is negative, NaN, or infinity
     * @retval StatusCode::Io Decoder seek failed
     * @retval StatusCode::Internal Decoder not initialized
     *
     * Thread Safety: Thread-safe (atomic operations for position, mutex for decode thread wake)
     * Gapless: Ring buffer is reset to avoid stale samples from previous position
     */
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

    /**
     * @brief Clamp volume value to valid range [0.0, 1.0]
     * @param v Volume value to clamp
     * @return Clamped volume value
     *
     * Handles NaN/infinity by returning 0.0. Used internally for
     * volume sanitization before applying to audio output.
     *
     * Thread Safety: Thread-safe (pure function, no shared state)
     */
    static inline float clampVolume(float v) noexcept {
        if (!std::isfinite(v))
            return 0.0f;
        return std::clamp(v, 0.0f, 1.0f);
    }

    /**
     * @brief Set playback volume
     * @param volume Volume level (0.0 = mute, 1.0 = full)
     * @return void on success, Error on failure
     *
     * Volume is clamped to [0.0, 1.0] and applied atomically to both
     * the internal volume state and the audio output device.
     *
     * @retval StatusCode::InvalidArg volume is NaN or infinity
     *
     * Thread Safety: Thread-safe (atomic store with relaxed ordering)
     */
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

    /**
     * @brief Get current playback state
     * @return Current State enum value
     *
     * Thread Safety: Thread-safe (atomic load with acquire ordering)
     */
    State state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get current playback position
     * @return Duration from stream start (0 if stopped/error)
     *
     * Calculates position from posBase (frames at last seek/start) plus
     * elapsed time since playback started. Uses steady_clock for monotonicity.
     *
     * Thread Safety: Thread-safe (atomic loads with acquire/relaxed ordering)
     */
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

    /**
     * @brief Get last error message
     * @return String view of the last error message (empty if none)
     *
     * Returns the error message from the last failed operation.
     * Cleared on successful open().
     *
     * Thread Safety: Thread-safe (atomic state + string access from control thread)
     */
    std::string_view lastError() const noexcept {
        return lastError_;
    }

  private:
    Player() = default;

    /**
     * @brief Initialize player with options
     * @param opts Configuration options
     * @return true on success, false on failure
     *
     * Sets up sample rate, channels, volume, and starts the decode thread.
     * Called by create() during factory construction.
     *
     * Thread Safety: Called once during construction (single-threaded)
     */
    bool init(const PlayerOpts& opts) {
        sampleRate_ = opts.sampleRate ? opts.sampleRate : 48000;
        channels_ = opts.channels ? opts.channels : 2;
        volume_.store(1.0f, std::memory_order_relaxed);
        state_.store(State::Stopped, std::memory_order_relaxed);

        // Start decode thread
        decodeThread_ = std::jthread([this](std::stop_token st) { decodeLoop(st); });

        return true;
    }

    /**
     * @brief Internal stop implementation (no mutex)
     * @return void on success, Error on failure
     *
     * Stops playback, resets all state, and wakes the decode thread.
     * Called by public stop() and openReader() (which holds openMutex_).
     *
     * Thread Safety: Must be called with openMutex_ held
     */
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

    /**
     * @brief Preroll ring buffer before playback starts
     *
     * Fills the ring buffer to half capacity (cap/2) by decoding
     * initial audio frames. This ensures immediate audio output when
     * play() starts the audio device, achieving gapless startup.
     *
     * Chunk size follows C ca_player.c: min(avail, 1024, 2048/channels)
     *
     * Thread Safety: Called from control thread during open() (single-threaded)
     */
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

    /**
     * @brief Decode thread main loop
     * @param st Stop token for graceful shutdown
     *
     * Runs on dedicated jthread. Waits for openGate and decodeBusy signals,
     * then decodes audio frames into the ring buffer. Handles:
     * - Ring buffer backpressure (sleeps when full)
     * - EOF detection (transitions to Stopped)
     * - Dynamic chunk sizing based on available space and channel count
     *
     * Chunk size: min(available, 1024, 2048/channels) frames per iteration
     *
     * Thread Safety: Runs on decode thread only. Synchronizes with control
     * thread via atomics (openGate, decodeBusy, isPlaying) and cv_.
     */
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

    /**
     * @brief Get current time in milliseconds (monotonic)
     * @return Milliseconds since epoch (steady_clock)
     *
     * Used for position tracking and elapsed time calculations.
     * steady_clock ensures monotonicity across system time changes.
     *
     * Thread Safety: Thread-safe (pure function)
     */
    static int64_t nowMs() noexcept {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    // Configuration
    /// Output sample rate (from decoder or opts)
    uint32_t sampleRate_{48000};
    /// Output channel count (from decoder or opts)
    uint32_t channels_{2};

    // Playback components
    /// Input reader (FileReader, MemoryReader, etc.)
    std::unique_ptr<Reader> reader_;
    /// Format decoder (FFmpeg, etc.)
    std::unique_ptr<IDecoder> decoder_;
    /// Lock-free SPSC ring buffer for decode/audio thread communication
    std::unique_ptr<caudio::utils::SpscRing<float>> ring_;
    /// Audio output device (miniaudio)
    std::unique_ptr<AudioOutput> output_;

    // Threading
    /// Dedicated decode thread
    std::jthread decodeThread_;
    /// Mutex for condition variable
    std::mutex cvMutex_;
    /// Condition variable for decode thread wakeup
    std::condition_variable cv_;
    /// Mutex for open/stop mutual exclusion
    std::mutex openMutex_;

    // Atomic state (C ca_player.c atomic fields)
    /// Current playback state
    std::atomic<State> state_{State::Stopped};
    /// Frame position base (at last seek/start)
    std::atomic<uint64_t> posBase_{0};
    /// Milliseconds when playback started (0 if paused/stopped)
    std::atomic<int64_t> posStartMs_{0};
    /// True when actively playing (audio device running)
    std::atomic<bool> isPlaying_{false};
    /// True when player is open (decoder/reader/output valid)
    std::atomic<bool> openGate_{false};
    /// True when decode thread should decode
    std::atomic<bool> decodeBusy_{false};
    /// Current volume (0.0 to 1.0)
    std::atomic<float> volume_{1.0f};

    /// Last error message (for lastError())
    std::string lastError_;
};

} // namespace caudio::player
