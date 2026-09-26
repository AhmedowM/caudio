#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
// Lightweight Windows forward decls — avoid including windows.h (HMODULE conflict)
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

#include <caudio/engine.hpp>
#include <caudio/utils.hpp>

namespace caudio::service {

// ShmStatus uses regular types for the snapshot (no atomics in the snapshot itself)
// The atomic fields are in the shared memory, but we read them into a non-atomic snapshot
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

// Atomic view of ShmStatus in shared memory
// Uses atomic<uint64_t> with bit_cast for double/float to ensure lock-free on all platforms
// including MSVC
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

// Helpers for bit_cast atomic operations
inline double atomicLoadDouble(const std::atomic<uint64_t>& a) noexcept;
inline void atomicStoreDouble(std::atomic<uint64_t>& a, double v) noexcept;
inline float atomicLoadFloat(const std::atomic<uint32_t>& a) noexcept;
inline void atomicStoreFloat(std::atomic<uint32_t>& a, float v) noexcept;

class ShmStatusHandle {
  public:
    using ExpectedShm = std::expected<ShmStatusHandle, caudio::utils::Error>;

    static ExpectedShm create(const std::string& name, bool create);
    static ExpectedShm openReadOnly(const std::string& name);

    ~ShmStatusHandle();

    ShmStatusHandle(const ShmStatusHandle&) = delete;
    ShmStatusHandle& operator=(const ShmStatusHandle&) = delete;

    ShmStatusHandle(ShmStatusHandle&& other) noexcept;
    ShmStatusHandle& operator=(ShmStatusHandle&& other) noexcept;

    bool valid() const noexcept;
    const std::string& name() const noexcept;

    // Seqlock reader: returns a snapshot copy of the status
    ShmStatus snapshot() const;

    // Seqlock writer: updates all fields atomically
    void updateFromEngine(const caudio::engine::Engine& eng, int64_t track_id,
                          std::string_view title, std::string_view artist);

    void setDuration(double dur) noexcept;
    void setQueueSize(size_t sz) noexcept;

  private:
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

// Convenience function for service to create shm status
ShmStatusHandle::ExpectedShm createShmStatus(const std::string& hash, bool create);

} // namespace caudio::service
