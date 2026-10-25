#pragma once

/**
 * @file shm_status.hpp
 * @brief Seqlock shared-memory playback snapshot (daemon writes, clients poll).
 * @ingroup caudio_ipc
 * @details Lets clients read the latest event-driven
 * position/state snapshot without an IPC round-trip. The daemon publishes
 * on dispatch only (no periodic writer), so position can go stale between
 * commands -- use the IPC status path when freshness matters. Writers publish
 * (lock-free atomics + seqlock sequence); readers get a plain `ShmStatus`
 * copy.
 *
 * This is the shared set of types; the writer takes plain data via
 * `ShmStatusUpdate` so `ipc` never depends on `engine::Engine` (the daemon
 * extracts the fields). Windows pattern as in the internal
 * `service/ipc_server.hpp`: local API declarations unless `windows.h` is
 * already included.
 */

#include <atomic>
#include <caudio/utils/error.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace caudio::ipc {

/**
 * @brief Plain snapshot copy (no atomics); what readers take home.
 * @ingroup caudio_ipc
 * @details Strings are fixed `char` arrays (atomics can't hold them);
 * writers publish under an odd seqlock sequence, readers retry on mismatch.
 */
struct ShmStatus {
    uint64_t seq{0};      // seqlock writer: odd=writing, even=done
    double position{0.0}; // current playback position in seconds
    double duration{0.0}; // track duration in seconds
    float volume{1.0f};   // volume 0.0-1.0
    bool muted{false};    // muted flag
    int state{0};         // PlaybackState enum (int)
    int64_t track_id{0};  // current track ID
    size_t queueSize{0};  // queue size
    char title[256]{0};   // track title
    char artist[256]{0};  // track artist
};

/**
 * @brief Plain data handed to `ShmStatusWriter::publish` by the daemon.
 * @ingroup caudio_ipc
 * @details Decouples the writer from `engine::Engine` (the daemon extracts
 * these fields; `ipc` never sees the engine). `title`/`artist` are views into
 * caller-owned storage, copied into the fixed arrays under the seqlock.
 */
struct ShmStatusUpdate final {
    double position{0.0}; // current playback position in seconds
    float volume{1.0f};   // volume 0.0-1.0
    bool muted{false};    // muted flag
    int state{0};         // PlaybackState enum (int)
    int64_t track_id{0};  // current track ID
    std::string_view title;
    std::string_view artist;
};

/**
 * @brief Lock-free shared-memory view (`bit_cast` doubles/floats through integers).
 * @ingroup caudio_ipc
 * @details Cache-line aligned; fits one page (static_assert below).
 */
struct alignas(64) AtomicShmStatus {
    std::atomic<uint64_t> seq{0};
    std::atomic<uint64_t> position{0}; // bit_cast<double>
    std::atomic<uint64_t> duration{0}; // bit_cast<double>
    std::atomic<uint32_t> volume{0};   // bit_cast<float>
    std::atomic<bool> muted{false};
    std::atomic<int> state{0};
    std::atomic<int64_t> track_id{0};
    std::atomic<size_t> queueSize{0};
    // Strings can't be atomic, use char arrays with seqlock protection
    char title[256]{0};
    char artist[256]{0};
};

static_assert(sizeof(AtomicShmStatus) <= 4096, "AtomicShmStatus should fit in one page");

/**
 * @brief `bit_cast` atomic helpers for the double/float snapshot fields.
 * @ingroup caudio_ipc
 */
inline double atomicLoadDouble(const std::atomic<uint64_t>& a) noexcept;
/** @ingroup caudio_ipc */
inline void atomicStoreDouble(std::atomic<uint64_t>& a, double v) noexcept;
/** @ingroup caudio_ipc */
inline float atomicLoadFloat(const std::atomic<uint32_t>& a) noexcept;
/** @ingroup caudio_ipc */
inline void atomicStoreFloat(std::atomic<uint32_t>& a, float v) noexcept;

/**
 * @brief Client-side read view of the shared-memory status segment.
 * @ingroup caudio_ipc
 * @details Exposes only snapshot reads.
 */
class ShmStatusReader {
  public:
    /** @brief Result of opening a segment. */
    using ExpectedShm = std::expected<ShmStatusReader, caudio::utils::Error>;

    /**
     * @brief Opens an existing segment read-only.
     * @ingroup caudio_ipc
     * @param name Platform segment name (derived from the database path).
     */
    static ExpectedShm openReadOnly(const std::string& name);

    /** @brief Unmaps and closes (idempotent). */
    ~ShmStatusReader();

    ShmStatusReader(const ShmStatusReader&) = delete;
    ShmStatusReader& operator=(const ShmStatusReader&) = delete;

    /** @brief Move-constructs, transferring the mapping. */
    ShmStatusReader(ShmStatusReader&& other) noexcept;
    /** @brief Move-assigns, transferring the mapping. */
    ShmStatusReader& operator=(ShmStatusReader&& other) noexcept;

    /** @brief True when the mapping is live. */
    bool valid() const noexcept;
    /** @brief Segment name. */
    const std::string& name() const noexcept;
    /** @brief Seqlock reader: returns a snapshot copy of the status. */
    ShmStatus snapshot() const;

  private:
    /** @brief Private default ctor; use openReadOnly(). */
    ShmStatusReader() = default;

    void close() noexcept;

    AtomicShmStatus* map_{nullptr};
    std::size_t size_{0};
    std::string name_;
    std::intptr_t handle_{-1};
};

/**
 * @brief Daemon-side write view of the shared-memory status segment.
 * @ingroup caudio_ipc
 * @details Exposes only publishing.
 */
class ShmStatusWriter {
  public:
    /** @brief Result of creating a segment. */
    using ExpectedShm = std::expected<ShmStatusWriter, caudio::utils::Error>;

    /**
     * @brief Creates the segment by name.
     * @ingroup caudio_ipc
     * @param name Platform segment name (derived from the database path).
     */
    static ExpectedShm create(const std::string& name);

    /** @brief Unmaps and closes (idempotent). */
    ~ShmStatusWriter();

    ShmStatusWriter(const ShmStatusWriter&) = delete;
    ShmStatusWriter& operator=(const ShmStatusWriter&) = delete;

    /** @brief Move-constructs, transferring the mapping. */
    ShmStatusWriter(ShmStatusWriter&& other) noexcept;
    /** @brief Move-assigns, transferring the mapping. */
    ShmStatusWriter& operator=(ShmStatusWriter&& other) noexcept;

    /** @brief True when the mapping is live. */
    bool valid() const noexcept;
    /** @brief Segment name. */
    const std::string& name() const noexcept;

    /**
     * @brief Seqlock writer: publishes position/volume/state/track/title/artist.
     * @param u Plain data; no `engine::Engine` needed at this layer.
     */
    void publish(const ShmStatusUpdate& u) noexcept;

    /** @brief Publishes the track duration. */
    void setDuration(double dur) noexcept;
    /** @brief Publishes the queue size. */
    void setQueueSize(size_t sz) noexcept;

  private:
    /** @brief Private default ctor; use create(). */
    ShmStatusWriter() = default;

    void close() noexcept;

    AtomicShmStatus* map_{nullptr};
    std::size_t size_{0};
    std::string name_;
    bool create_{false};
    std::intptr_t handle_{-1};
};

} // namespace caudio::ipc
