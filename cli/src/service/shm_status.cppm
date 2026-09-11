module;
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
#include <windows.h>
#endif

export module caudio.service:shm_status;

import caudio.utils;
import caudio.engine;

export namespace caudio::service {

// ShmStatus uses regular types for the snapshot (no atomics in the snapshot itself)
// The atomic fields are in the shared memory, but we read them into a non-atomic snapshot
struct ShmStatus {
    uint64_t seq{0};           // seqlock writer: odd=writing, even=done
    double position{0.0};      // current playback position in seconds
    double duration{0.0};      // track duration in seconds
    float volume{1.0f};        // volume 0.0-1.0
    bool muted{false};         // muted flag
    int state{0};              // PlaybackState enum (int)
    int64_t trackId{0};        // current track ID
    size_t queueSize{0};       // queue size
    char title[256]{0};        // track title
    char artist[256]{0};       // track artist
};

// Atomic view of ShmStatus in shared memory
// Uses atomic<uint64_t> with bit_cast for double/float to ensure lock-free on all platforms including MSVC
struct alignas(64) AtomicShmStatus {
    std::atomic<uint64_t> seq{0};
    std::atomic<uint64_t> position{0};  // bit_cast<double>
    std::atomic<uint64_t> duration{0};  // bit_cast<double>
    std::atomic<uint32_t> volume{0};    // bit_cast<float>
    std::atomic<bool> muted{false};
    std::atomic<int> state{0};
    std::atomic<int64_t> trackId{0};
    std::atomic<size_t> queueSize{0};
    // Strings can't be atomic, use char arrays with seqlock protection
    char title[256]{0};
    char artist[256]{0};
};

static_assert(sizeof(AtomicShmStatus) <= 4096, "AtomicShmStatus should fit in one page");

// Helpers for bit_cast atomic operations
inline double atomicLoadDouble(const std::atomic<uint64_t>& a) noexcept {
    return std::bit_cast<double>(a.load(std::memory_order_acquire));
}
inline void atomicStoreDouble(std::atomic<uint64_t>& a, double v) noexcept {
    a.store(std::bit_cast<uint64_t>(v), std::memory_order_relaxed);
}
inline float atomicLoadFloat(const std::atomic<uint32_t>& a) noexcept {
    return std::bit_cast<float>(a.load(std::memory_order_acquire));
}
inline void atomicStoreFloat(std::atomic<uint32_t>& a, float v) noexcept {
    a.store(std::bit_cast<uint32_t>(v), std::memory_order_relaxed);
}

class ShmStatusHandle {
public:
    using ExpectedShm = std::expected<ShmStatusHandle, caudio::utils::Error>;

    static ExpectedShm create(const std::string& name, bool create) {
        std::string fullName = "caudio-" + name + "-status";
        ShmStatusHandle handle;
        handle.name_ = std::move(fullName);
        handle.create_ = create;

#ifndef _WIN32
        int flags = O_RDWR | O_CLOEXEC;
        if (create) flags |= O_CREAT;
        int fd = ::shm_open(handle.name_.c_str(), flags, 0600);
        if (fd < 0) {
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::Result::Io,
                "shm_open failed: " + std::string(std::strerror(errno)))};
        }
        handle.fd_ = fd;

        if (create) {
            if (::ftruncate(fd, sizeof(AtomicShmStatus)) != 0) {
                ::close(fd);
                ::shm_unlink(handle.name_.c_str());
                return std::unexpected{caudio::utils::makeError(
                    caudio::utils::Result::Io,
                    "ftruncate failed: " + std::string(std::strerror(errno)))};
            }
        }

        void* ptr = ::mmap(nullptr, sizeof(AtomicShmStatus), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            ::close(fd);
            if (create) ::shm_unlink(handle.name_.c_str());
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::Result::Io,
                "mmap failed: " + std::string(std::strerror(errno)))};
        }
        handle.map_ = static_cast<AtomicShmStatus*>(ptr);
        handle.size_ = sizeof(AtomicShmStatus);
#else
        std::wstring wname;
        wname.reserve(handle.name_.size());
        for (char c : handle.name_) wname.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));

        DWORD access = FILE_MAP_READ | FILE_MAP_WRITE;
        DWORD protect = PAGE_READWRITE;
        HANDLE hMap = nullptr;

        if (create) {
            hMap = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, protect, 0, sizeof(AtomicShmStatus), wname.c_str());
        } else {
            hMap = ::OpenFileMappingW(access, FALSE, wname.c_str());
        }

        if (!hMap) {
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::Result::Io,
                "CreateFileMapping/OpenFileMapping failed: " + std::to_string(::GetLastError()))};
        }
        handle.hMap_ = hMap;

        void* ptr = ::MapViewOfFile(hMap, access, 0, 0, sizeof(AtomicShmStatus));
        if (!ptr) {
            ::CloseHandle(hMap);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::Result::Io,
                "MapViewOfFile failed: " + std::to_string(::GetLastError()))};
        }
        handle.map_ = static_cast<AtomicShmStatus*>(ptr);
        handle.size_ = sizeof(AtomicShmStatus);
#endif

        return handle;
    }

    static ExpectedShm openReadOnly(const std::string& name) {
        return create(name, false);
    }

    ~ShmStatusHandle() {
        close();
    }

    ShmStatusHandle(const ShmStatusHandle&) = delete;
    ShmStatusHandle& operator=(const ShmStatusHandle&) = delete;

    ShmStatusHandle(ShmStatusHandle&& other) noexcept
        : map_(other.map_), size_(other.size_), name_(std::move(other.name_)),
          create_(other.create_)
#ifndef _WIN32
          , fd_(other.fd_)
#else
          , hMap_(other.hMap_)
#endif
    {
        other.map_ = nullptr;
        other.size_ = 0;
#ifndef _WIN32
        other.fd_ = -1;
#else
        other.hMap_ = nullptr;
#endif
        other.create_ = false;
    }

    ShmStatusHandle& operator=(ShmStatusHandle&& other) noexcept {
        if (this != &other) {
            close();
            map_ = other.map_;
            size_ = other.size_;
            name_ = std::move(other.name_);
            create_ = other.create_;
#ifndef _WIN32
            fd_ = other.fd_;
#else
            hMap_ = other.hMap_;
#endif
            other.map_ = nullptr;
            other.size_ = 0;
#ifndef _WIN32
            other.fd_ = -1;
#else
            other.hMap_ = nullptr;
#endif
            other.create_ = false;
        }
        return *this;
    }

    bool valid() const noexcept { return map_ != nullptr; }

    const std::string& name() const noexcept { return name_; }

    // Seqlock reader: returns a snapshot copy of the status
    ShmStatus snapshot() const {
        ShmStatus out{};
        if (!map_) return out;
        AtomicShmStatus* s = map_;
        uint32_t spinCount = 0;
        while (true) {
            uint64_t seq = s->seq.load(std::memory_order_acquire);
            if (seq & 1) {
                // Writer busy, retry with occasional yield
                if (++spinCount >= 3) {
                    std::this_thread::yield();
                    spinCount = 0;
                }
                continue;
            }
            out.seq = seq;
            out.position = atomicLoadDouble(s->position);
            out.duration = atomicLoadDouble(s->duration);
            out.volume = atomicLoadFloat(s->volume);
            out.muted = s->muted.load(std::memory_order_acquire);
            out.state = s->state.load(std::memory_order_acquire);
            out.trackId = s->trackId.load(std::memory_order_acquire);
            out.queueSize = s->queueSize.load(std::memory_order_acquire);
            // Copy strings (protected by seqlock)
            std::memcpy(out.title, s->title, sizeof(out.title));
            std::memcpy(out.artist, s->artist, sizeof(out.artist));
            // Verify no writer intervened
            if (s->seq.load(std::memory_order_acquire) == seq) {
                break;
            }
        }
        return out;
    }

    // Seqlock writer: updates all fields atomically
    void updateFromEngine(const caudio::engine::Engine& eng, int64_t trackId,
                          std::string_view title, std::string_view artist) {
        if (!map_) return;
        AtomicShmStatus* s = map_;
        uint64_t old = s->seq.load(std::memory_order_acquire);
        // Ensure old is even
        if (old & 1) old++;

        // Mark as writing (odd)
        s->seq.store(old + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);

        atomicStoreDouble(s->position, eng.position());
        atomicStoreFloat(s->volume, eng.volume());
        s->state.store(static_cast<int>(eng.state()), std::memory_order_relaxed);
        s->trackId.store(trackId, std::memory_order_relaxed);
        s->muted.store(eng.volume() == 0.0f, std::memory_order_relaxed);

        // Copy strings with bounds checking
        if (!title.empty()) {
            std::size_t n = std::min(title.size(), sizeof(s->title) - 1);
            std::memcpy(s->title, title.data(), n);
            s->title[n] = '\0';
        }
        if (!artist.empty()) {
            std::size_t n = std::min(artist.size(), sizeof(s->artist) - 1);
            std::memcpy(s->artist, artist.data(), n);
            s->artist[n] = '\0';
        }

        // Mark as done (even)
        s->seq.store(old + 2, std::memory_order_release);
    }

    void setDuration(double dur) noexcept {
        if (!map_) return;
        atomicStoreDouble(map_->duration, dur);
    }

    void setQueueSize(size_t sz) noexcept {
        if (!map_) return;
        map_->queueSize.store(sz, std::memory_order_relaxed);
    }

private:
    ShmStatusHandle() = default;

    void close() noexcept {
        if (map_) {
#ifndef _WIN32
            ::munmap(map_, size_);
            map_ = nullptr;
            if (fd_ >= 0) {
                if (create_) {
                    ::shm_unlink(name_.c_str());
                }
                ::close(fd_);
                fd_ = -1;
            }
#else
            ::UnmapViewOfFile(map_);
            map_ = nullptr;
            if (hMap_) {
                ::CloseHandle(hMap_);
                hMap_ = nullptr;
            }
#endif
        }
    }

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
inline ShmStatusHandle::ExpectedShm createShmStatus(const std::string& hash, bool create) {
    return ShmStatusHandle::create(hash, create);
}

} // namespace caudio::service