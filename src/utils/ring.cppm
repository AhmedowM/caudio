module;
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

export module caudio.utils:ring;

export namespace caudio::utils {

// SPSC ring: single-producer / single-consumer.
// - wr / rd are cache-line padded atomics (see members below).
// - write() is producer-only: wr_.load(relaxed) (writer owns wr), rd_.load(acquire).
// - read()  is consumer-only: rd_.load(relaxed) (reader owns rd), wr_.load(acquire).
// - reset() requires external synchronization: caller must ensure producer & consumer
//   are stopped/paused or hold decodeMtx_ (engine's decodeMtx_) before calling.
//   No internal lock; concurrent reset with read/write is a data race.
//   gaplessArmed_ semantics: 0→1 CAS arms gapless transition 300ms before track end.
template <typename T>
class SpscRing {
  public:
    SpscRing() = delete;
    SpscRing(std::size_t capacityFrames, std::uint32_t channels = 1)
        : cap_(capacityFrames), channels_(channels), buf_(capacityFrames * channels) {
        // capacityFrames ==0 is allowed? The C API returned InvalidArg. In C++ we ensure cap>0.
        // If cap==0, buffer empty and all ops no-op.
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept {
        return cap_;
    }
    [[nodiscard]] std::uint32_t channels() const noexcept {
        return channels_;
    }

    std::size_t write(std::span<const T> data) noexcept {
        if (data.empty() || cap_ == 0)
            return 0;
        std::size_t frames = data.size() / channels_;
        if (frames == 0)
            return 0;
        return write(data.data(), frames);
    }

    std::size_t write(const T* data, std::size_t frames) noexcept {
        if (!data || frames == 0 || cap_ == 0)
            return 0;
        std::size_t wr = wr_.load(std::memory_order_relaxed); // writer owns wr
        std::size_t rd = rd_.load(std::memory_order_acquire);
        std::size_t used = wr - rd;
        if (used > cap_)
            used = cap_;
        std::size_t free = cap_ - used;
        if (free == 0)
            return 0;
        if (frames > free)
            frames = free;
        std::size_t wrIdx = wr % cap_;
        std::size_t ch = static_cast<std::size_t>(channels_);
        std::size_t first = cap_ - wrIdx;
        if (first > frames)
            first = frames;
        // memcpy wrap like ca_ring.c:89
        std::memcpy(buf_.data() + wrIdx * ch, data, first * ch * sizeof(T));
        if (frames > first) {
            std::memcpy(buf_.data(), data + first * ch, (frames - first) * ch * sizeof(T));
        }
        wr_.store(wr + frames, std::memory_order_release);
        return frames;
    }

    std::size_t read(std::span<T> out) noexcept {
        if (out.empty() || cap_ == 0)
            return 0;
        std::size_t frames = out.size() / channels_;
        if (frames == 0)
            return 0;
        return read(out.data(), frames);
    }

    std::size_t read(T* out, std::size_t frames) noexcept {
        if (!out || frames == 0 || cap_ == 0)
            return 0;
        std::size_t rd = rd_.load(std::memory_order_relaxed); // reader owns rd
        std::size_t wr = wr_.load(std::memory_order_acquire);
        std::size_t avail = wr - rd;
        if (avail > cap_)
            avail = cap_;
        if (avail == 0)
            return 0;
        if (frames > avail)
            frames = avail;
        std::size_t rdIdx = rd % cap_;
        std::size_t ch = static_cast<std::size_t>(channels_);
        std::size_t first = cap_ - rdIdx;
        if (first > frames)
            first = frames;
        std::memcpy(out, buf_.data() + rdIdx * ch, first * ch * sizeof(T));
        if (frames > first) {
            std::memcpy(out + first * ch, buf_.data(), (frames - first) * ch * sizeof(T));
        }
        rd_.store(rd + frames, std::memory_order_release);
        return frames;
    }

    [[nodiscard]] std::size_t availableRead() const noexcept {
        // consumer view: rd relaxed (owned), wr acquire (sync with producer)
        std::size_t rd = rd_.load(std::memory_order_relaxed);
        std::size_t wr = wr_.load(std::memory_order_acquire);
        std::size_t avail = wr - rd;
        if (avail > cap_)
            avail = cap_;
        return avail;
    }

    [[nodiscard]] std::size_t availableWrite() const noexcept {
        // producer view: wr relaxed (owned), rd acquire (sync with consumer)
        std::size_t wr = wr_.load(std::memory_order_relaxed);
        std::size_t rd = rd_.load(std::memory_order_acquire);
        std::size_t avail = wr - rd;
        if (avail > cap_)
            avail = cap_;
        return cap_ - avail;
    }

    // Requires external sync: producer & consumer stopped or decodeMtx_ held.
    // Caller (Engine::seek/stop/play) must hold decodeMtx_ or ensure decode thread paused.
    void reset() noexcept {
        // No fence needed: caller guarantees no concurrent read/write.
        rd_.store(0, std::memory_order_release);
        wr_.store(0, std::memory_order_release);
    }

  private:
    std::size_t cap_{0};
    std::uint32_t channels_{1};
    std::vector<T> buf_{};
    alignas(64) std::atomic<std::size_t> wr_{0};
    alignas(64) std::atomic<std::size_t> rd_{0};
};

} // namespace caudio::utils
