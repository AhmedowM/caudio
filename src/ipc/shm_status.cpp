#include <algorithm>
#include <atomic>
#include <bit>
#include <caudio/ipc/shm_status.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <caudio/windows_fwd.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace caudio::ipc {

// Helpers for bit_cast atomic operations
double atomicLoadDouble(const std::atomic<uint64_t>& a) noexcept {
    return std::bit_cast<double>(a.load(std::memory_order_acquire));
}
void atomicStoreDouble(std::atomic<uint64_t>& a, double v) noexcept {
    a.store(std::bit_cast<uint64_t>(v), std::memory_order_relaxed);
}
float atomicLoadFloat(const std::atomic<uint32_t>& a) noexcept {
    return std::bit_cast<float>(a.load(std::memory_order_acquire));
}
void atomicStoreFloat(std::atomic<uint32_t>& a, float v) noexcept {
    a.store(std::bit_cast<uint32_t>(v), std::memory_order_relaxed);
}

ShmStatusHandle::ExpectedShm ShmStatusHandle::create(const std::string& name, bool create) {
    std::string fullName = "caudio-" + name + "-status";
    ShmStatusHandle handle;
    handle.name_ = std::move(fullName);
    handle.create_ = create;

#ifndef _WIN32
    int flags = O_RDWR | O_CLOEXEC;
    if (create)
        flags |= O_CREAT;
    int fd = ::shm_open(handle.name_.c_str(), flags, 0600);
    if (fd < 0) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                     "shm_open failed: " + std::string(std::strerror(errno)))};
    }
    handle.handle_ = fd;

    if (create) {
        if (::ftruncate(fd, sizeof(AtomicShmStatus)) != 0) {
            ::close(fd);
            ::shm_unlink(handle.name_.c_str());
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                         "ftruncate failed: " + std::string(std::strerror(errno)))};
        }
    }

    void* ptr = ::mmap(nullptr, sizeof(AtomicShmStatus), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        ::close(fd);
        if (create)
            ::shm_unlink(handle.name_.c_str());
        return std::unexpected{caudio::utils::makeError(
            caudio::utils::StatusCode::Io, "mmap failed: " + std::string(std::strerror(errno)))};
    }
    handle.map_ = static_cast<AtomicShmStatus*>(ptr);
    handle.size_ = sizeof(AtomicShmStatus);
#else
    std::wstring wname;
    wname.reserve(handle.name_.size());
    for (char c : handle.name_)
        wname.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));

    DWORD access = kFileMapRead | kFileMapWrite;
    DWORD protect = kPageReadWrite;
    HANDLE hMap = nullptr;

    if (create) {
        hMap = ::CreateFileMappingW(kInvalidHandleValue, nullptr, protect, 0,
                                    sizeof(AtomicShmStatus), wname.c_str());
    } else {
        hMap = ::OpenFileMappingW(access, kFalse, wname.c_str());
    }

    if (!hMap) {
        return std::unexpected{caudio::utils::makeError(
            caudio::utils::StatusCode::Io,
            "CreateFileMapping/OpenFileMapping failed: " + std::to_string(::GetLastError()))};
    }
    handle.handle_ = reinterpret_cast<std::intptr_t>(hMap);

    void* ptr = ::MapViewOfFile(hMap, access, 0, 0, sizeof(AtomicShmStatus));
    if (!ptr) {
        ::CloseHandle(hMap);
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                     "MapViewOfFile failed: " + std::to_string(::GetLastError()))};
    }
    handle.map_ = static_cast<AtomicShmStatus*>(ptr);
    handle.size_ = sizeof(AtomicShmStatus);
#endif

    return handle;
}

ShmStatusHandle::ExpectedShm ShmStatusHandle::openReadOnly(const std::string& name) {
    return create(name, false);
}

ShmStatusHandle::~ShmStatusHandle() {
    close();
}

ShmStatusHandle::ShmStatusHandle(ShmStatusHandle&& other) noexcept
    : map_(other.map_), size_(other.size_), name_(std::move(other.name_)), create_(other.create_),
      handle_(other.handle_) {
    other.map_ = nullptr;
    other.size_ = 0;
    other.handle_ = -1;
    other.create_ = false;
}

ShmStatusHandle& ShmStatusHandle::operator=(ShmStatusHandle&& other) noexcept {
    if (this != &other) {
        close();
        map_ = other.map_;
        size_ = other.size_;
        name_ = std::move(other.name_);
        create_ = other.create_;
        handle_ = other.handle_;
        other.map_ = nullptr;
        other.size_ = 0;
        other.handle_ = -1;
        other.create_ = false;
    }
    return *this;
}

bool ShmStatusHandle::valid() const noexcept {
    return map_ != nullptr;
}

const std::string& ShmStatusHandle::name() const noexcept {
    return name_;
}

// Seqlock reader: returns a snapshot copy of the status
ShmStatus ShmStatusHandle::snapshot() const {
    ShmStatus out{};
    if (!map_)
        return out;
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
        out.track_id = s->track_id.load(std::memory_order_acquire);
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

// Seqlock writer: publishes the plain update under an odd/even sequence.
void ShmStatusHandle::publish(const ShmStatusUpdate& u) noexcept {
    if (!map_)
        return;
    AtomicShmStatus* s = map_;
    uint64_t old = s->seq.load(std::memory_order_acquire);
    // Ensure old is even
    if (old & 1)
        old++;

    // Mark as writing (odd)
    s->seq.store(old + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);

    atomicStoreDouble(s->position, u.position);
    atomicStoreFloat(s->volume, u.volume);
    s->state.store(u.state, std::memory_order_relaxed);
    s->track_id.store(u.track_id, std::memory_order_relaxed);
    s->muted.store(u.muted, std::memory_order_relaxed);

    // Copy strings with bounds checking
    if (!u.title.empty()) {
        std::size_t n = std::min(u.title.size(), sizeof(s->title) - 1);
        std::memcpy(s->title, u.title.data(), n);
        s->title[n] = '\0';
    }
    if (!u.artist.empty()) {
        std::size_t n = std::min(u.artist.size(), sizeof(s->artist) - 1);
        std::memcpy(s->artist, u.artist.data(), n);
        s->artist[n] = '\0';
    }

    // Mark as done (even)
    s->seq.store(old + 2, std::memory_order_release);
}

void ShmStatusHandle::setDuration(double dur) noexcept {
    if (!map_)
        return;
    atomicStoreDouble(map_->duration, dur);
}

void ShmStatusHandle::setQueueSize(size_t sz) noexcept {
    if (!map_)
        return;
    map_->queueSize.store(sz, std::memory_order_relaxed);
}

void ShmStatusHandle::close() noexcept {
    if (map_) {
#ifndef _WIN32
        ::munmap(map_, size_);
        map_ = nullptr;
        if (handle_ >= 0) {
            if (create_) {
                ::shm_unlink(name_.c_str());
            }
            ::close(static_cast<int>(handle_));
            handle_ = -1;
        }
#else
        ::UnmapViewOfFile(map_);
        map_ = nullptr;
        if (handle_ != -1) {
            ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
            handle_ = -1;
        }
#endif
    }
}

ShmStatusReader::ShmStatusReader(ShmStatusHandle handle) noexcept : handle_(std::move(handle)) {}

ShmStatusReader::ExpectedShm ShmStatusReader::openReadOnly(const std::string& name) {
    auto handle = ShmStatusHandle::openReadOnly(name);
    if (!handle)
        return std::unexpected{std::move(handle.error())};
    return ShmStatusReader{std::move(*handle)};
}

ShmStatusReader::~ShmStatusReader() = default;
ShmStatusReader::ShmStatusReader(ShmStatusReader&& other) noexcept = default;
ShmStatusReader& ShmStatusReader::operator=(ShmStatusReader&& other) noexcept = default;

bool ShmStatusReader::valid() const noexcept {
    return handle_.valid();
}

const std::string& ShmStatusReader::name() const noexcept {
    return handle_.name();
}

ShmStatus ShmStatusReader::snapshot() const {
    return handle_.snapshot();
}

ShmStatusWriter::ShmStatusWriter(ShmStatusHandle handle) noexcept : handle_(std::move(handle)) {}

ShmStatusWriter::ExpectedShm ShmStatusWriter::create(const std::string& name) {
    auto handle = ShmStatusHandle::create(name, true);
    if (!handle)
        return std::unexpected{std::move(handle.error())};
    return ShmStatusWriter{std::move(*handle)};
}

ShmStatusWriter::~ShmStatusWriter() = default;
ShmStatusWriter::ShmStatusWriter(ShmStatusWriter&& other) noexcept = default;
ShmStatusWriter& ShmStatusWriter::operator=(ShmStatusWriter&& other) noexcept = default;

bool ShmStatusWriter::valid() const noexcept {
    return handle_.valid();
}

const std::string& ShmStatusWriter::name() const noexcept {
    return handle_.name();
}

void ShmStatusWriter::publish(const ShmStatusUpdate& u) noexcept {
    handle_.publish(u);
}

void ShmStatusWriter::setDuration(double dur) noexcept {
    handle_.setDuration(dur);
}

void ShmStatusWriter::setQueueSize(size_t sz) noexcept {
    handle_.setQueueSize(sz);
}

ShmStatusHandle::ExpectedShm createShmStatus(const std::string& hash, bool create) {
    return ShmStatusHandle::create(hash, create);
}

} // namespace caudio::ipc
