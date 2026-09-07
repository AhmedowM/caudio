module;
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

export module caudio.utils:queue;

import :result;
import :error;

export namespace caudio::utils {

template <typename T>
class MpscQueue {
  public:
    explicit MpscQueue(std::size_t capacity = 64) : cap_(capacity), buf_(capacity) {
        if (cap_ == 0)
            cap_ = 1;
        if (buf_.size() != cap_)
            buf_.resize(cap_);
    }

    MpscQueue(const MpscQueue&) = delete;
    MpscQueue& operator=(const MpscQueue&) = delete;
    MpscQueue(MpscQueue&&) = delete;
    MpscQueue& operator=(MpscQueue&&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept {
        return cap_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        return wr_.load(std::memory_order_acquire) - rd_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        return wr_.load(std::memory_order_acquire) == rd_.load(std::memory_order_acquire);
    }

    [[nodiscard]] Expected<void> push(const T& value) {
        std::unique_lock<std::mutex> lk(mutex_);
        std::size_t wr = wr_.load(std::memory_order_acquire);
        std::size_t rd = rd_.load(std::memory_order_acquire);
        std::size_t used = wr - rd;
        if (used >= cap_) {
            return std::unexpected(Error{Result::Busy, "queue full"});
        }
        std::size_t idx = wr % cap_;
        buf_[idx] = value;
        wr_.store(wr + 1, std::memory_order_release);
        cv_.notify_one();
        return {};
    }

    [[nodiscard]] Expected<void> push(T&& value) {
        std::unique_lock<std::mutex> lk(mutex_);
        std::size_t wr = wr_.load(std::memory_order_acquire);
        std::size_t rd = rd_.load(std::memory_order_acquire);
        std::size_t used = wr - rd;
        if (used >= cap_) {
            return std::unexpected(Error{Result::Busy, "queue full"});
        }
        std::size_t idx = wr % cap_;
        buf_[idx] = std::move(value);
        wr_.store(wr + 1, std::memory_order_release);
        cv_.notify_one();
        return {};
    }

    template <typename... Args>
    [[nodiscard]] Expected<void> emplace(Args&&... args) {
        std::unique_lock<std::mutex> lk(mutex_);
        std::size_t wr = wr_.load(std::memory_order_acquire);
        std::size_t rd = rd_.load(std::memory_order_acquire);
        std::size_t used = wr - rd;
        if (used >= cap_) {
            return std::unexpected(Error{Result::Busy, "queue full"});
        }
        std::size_t idx = wr % cap_;
        buf_[idx] = T(std::forward<Args>(args)...);
        wr_.store(wr + 1, std::memory_order_release);
        cv_.notify_one();
        return {};
    }

    [[nodiscard]] Expected<T> pop() {
        std::unique_lock<std::mutex> lk(mutex_);
        std::size_t wr = wr_.load(std::memory_order_acquire);
        std::size_t rd = rd_.load(std::memory_order_acquire);
        if (wr == rd) {
            return std::unexpected(Error{Result::State, "queue empty"});
        }
        std::size_t idx = rd % cap_;
        T val = std::move(buf_[idx]);
        rd_.store(rd + 1, std::memory_order_release);
        return val;
    }

    // Blocking pop with stop support (optional, not used in tests)
    [[nodiscard]] std::optional<T> waitPop(std::stop_token st = {}) {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, st, [this] {
            std::size_t wr = wr_.load(std::memory_order_acquire);
            std::size_t rd = rd_.load(std::memory_order_acquire);
            return wr != rd;
        });
        if (st.stop_requested())
            return std::nullopt;
        std::size_t wr = wr_.load(std::memory_order_acquire);
        std::size_t rd = rd_.load(std::memory_order_acquire);
        if (wr == rd)
            return std::nullopt;
        std::size_t idx = rd % cap_;
        T val = std::move(buf_[idx]);
        rd_.store(rd + 1, std::memory_order_release);
        return val;
    }

  private:
    std::size_t cap_;
    std::vector<T> buf_;
    alignas(64) std::atomic<std::size_t> wr_{0};
    alignas(64) std::atomic<std::size_t> rd_{0};
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
};

} // namespace caudio::utils
