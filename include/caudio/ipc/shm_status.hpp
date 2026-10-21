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
 * @brief Plain data handed to `ShmStatusHandle::publish` by the daemon.
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
 * @brief RAII owner of the shared-memory segment (creator or reader side).
 * @ingroup caudio_ipc
 */
class ShmStatusHandle {
  public:
    using ExpectedShm = std::expected<ShmStatusHandle, caudio::utils::Error>;

    /**
     * @brief Creates or opens the segment by name.
     * @ingroup caudio_ipc
     * @param name Platform segment name (derived from the database path).
     * @param create True to create, false to open an existing one.
     */
    static ExpectedShm create(const std::string& name, bool create);
    /**
     * @brief Opens an existing segment read-only (client side).
     * @ingroup caudio_ipc
     */
    static ExpectedShm openReadOnly(const std::string& name);

    /** @brief Unmaps and closes (idempotent). */
    ~ShmStatusHandle();

    ShmStatusHandle(const ShmStatusHandle&) = delete;
    ShmStatusHandle& operator=(const ShmStatusHandle&) = delete;

    /** @brief Move-constructs, transferring the mapping. */
    ShmStatusHandle(ShmStatusHandle&& other) noexcept;
    /** @brief Move-assigns, transferring the mapping. */
    ShmStatusHandle& operator=(ShmStatusHandle&& other) noexcept;

    /** @brief True when the mapping is live. @ingroup caudio_ipc */
    bool valid() const noexcept;
    /** @brief Segment name. @ingroup caudio_ipc */
    const std::string& name() const noexcept;

    /**
     * @brief Seqlock reader: returns a snapshot copy of the status.
     * @ingroup caudio_ipc
     */
    ShmStatus snapshot() const;

    /**
     * @brief Seqlock writer: publishes position/volume/state/track/title/artist.
     * @ingroup caudio_ipc
     * @param u Plain data; no `engine::Engine` needed at this layer.
     */
    void publish(const ShmStatusUpdate& u) noexcept;

    /** @ingroup caudio_ipc */
    void setDuration(double dur) noexcept;
    /** @ingroup caudio_ipc */
    void setQueueSize(size_t sz) noexcept;

  private:
    /** @brief Private default ctor; use create()/openReadOnly(). */
    ShmStatusHandle() = default;

    void close() noexcept;

    AtomicShmStatus* map_{nullptr};
    std::size_t size_{0};
    std::string name_;
    bool create_{false};
    std::intptr_t handle_{-1};
};

/**
 * @brief Creates the daemon-side status segment for a database hash.
 * @ingroup caudio_ipc
 * @param hash Database identity hash (from the db path).
 * @param create True to create, false to open.
 */
ShmStatusHandle::ExpectedShm createShmStatus(const std::string& hash, bool create);

} // namespace caudio::ipc
