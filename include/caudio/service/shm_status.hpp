#pragma once

/**
 * @file shm_status.hpp
 * @brief Seqlock shared-memory playback snapshot (daemon writes, clients poll).
 * @ingroup caudio_service
 * @details Lets `Client::snapshotStatus()` read position/state at 10 fps
 * without an IPC round-trip. Writers publish through `AtomicShmStatus`
 * (lock-free atomics + seqlock sequence); readers get a plain `ShmStatus`
 * copy. Windows pattern as in `ipc_server.hpp`: local API declarations
 * unless `windows.h` is already included.
 */

#include <atomic>
#include <caudio/engine/core.hpp>
#include <caudio/utils/error.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
// Lightweight Windows forward decls -- avoid including windows.h (HMODULE conflict)
#if !defined(_WINDOWS_) && !defined(_WINDEF_) && !defined(_MINWINDEF_)
using HANDLE = void*;
using DWORD = unsigned long;
using BOOL = int;
using LPCWSTR = const wchar_t*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPDWORD = DWORD*;
using LPSECURITY_ATTRIBUTES = void*;
inline constexpr DWORD kFileMapReadW = 0x0004UL;
inline constexpr DWORD kFileMapWriteW = 0x0002UL;
inline constexpr DWORD kPageReadWriteW = 0x04UL;
inline const HANDLE kInvalidHandleValueW = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
extern "C" {
__declspec(dllimport) HANDLE __stdcall CreateFileMappingW(HANDLE, LPSECURITY_ATTRIBUTES, DWORD,
                                                          DWORD, DWORD, LPCWSTR);
__declspec(dllimport) HANDLE __stdcall OpenFileMappingW(DWORD, BOOL, LPCWSTR);
__declspec(dllimport) LPVOID __stdcall MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, std::size_t);
__declspec(dllimport) BOOL __stdcall UnmapViewOfFile(LPCVOID);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) DWORD __stdcall GetLastError();
}
#else
#include <windows.h>
#endif
#endif

namespace caudio::service {

/**
 * @brief Plain snapshot copy (no atomics); what readers take home.
 * @ingroup caudio_service
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
 * @brief Lock-free shared-memory view (`bit_cast` doubles/floats through integers).
 * @ingroup caudio_service
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
 * @ingroup caudio_service
 */
inline double atomicLoadDouble(const std::atomic<uint64_t>& a) noexcept;
/** @ingroup caudio_service */
inline void atomicStoreDouble(std::atomic<uint64_t>& a, double v) noexcept;
/** @ingroup caudio_service */
inline float atomicLoadFloat(const std::atomic<uint32_t>& a) noexcept;
/** @ingroup caudio_service */
inline void atomicStoreFloat(std::atomic<uint32_t>& a, float v) noexcept;

/**
 * @brief RAII owner of the shared-memory segment (creator or reader side).
 * @ingroup caudio_service
 */
class ShmStatusHandle {
  public:
    using ExpectedShm = std::expected<ShmStatusHandle, caudio::utils::Error>;

    /**
     * @brief Creates or opens the segment by name.
     * @ingroup caudio_service
     * @param name Platform segment name (derived from the database path).
     * @param create True to create, false to open an existing one.
     */
    static ExpectedShm create(const std::string& name, bool create);
    /**
     * @brief Opens an existing segment read-only (client side).
     * @ingroup caudio_service
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

    /** @brief True when the mapping is live. @ingroup caudio_service */
    bool valid() const noexcept;
    /** @brief Segment name. @ingroup caudio_service */
    const std::string& name() const noexcept;

    /**
     * @brief Seqlock reader: returns a snapshot copy of the status.
     * @ingroup caudio_service
     */
    ShmStatus snapshot() const;

    /**
     * @brief Seqlock writer: updates all fields atomically.
     * @ingroup caudio_service
     */
    void updateFromEngine(const caudio::engine::Engine& eng, int64_t track_id,
                          std::string_view title, std::string_view artist);

    /** @ingroup caudio_service */
    void setDuration(double dur) noexcept;
    /** @ingroup caudio_service */
    void setQueueSize(size_t sz) noexcept;

  private:
    /** @brief Private default ctor; use create()/openReadOnly(). */
    ShmStatusHandle() = default;

    void close() noexcept;

    AtomicShmStatus* map_{nullptr};
    std::size_t size_{0};
    std::string name_;
    bool create_{false};
#ifndef _WIN32
    int fd_{-1};
#else
    HANDLE hMap_{nullptr};
#endif
};

/**
 * @brief Creates the daemon-side status segment for a database hash.
 * @ingroup caudio_service
 * @param hash Database identity hash (from the db path).
 * @param create True to create, false to open.
 */
ShmStatusHandle::ExpectedShm createShmStatus(const std::string& hash, bool create);

} // namespace caudio::service
