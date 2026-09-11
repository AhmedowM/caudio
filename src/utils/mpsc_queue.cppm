module;
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

export module caudio.utils:mpsc_queue;

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
        return wr_ - rd_;
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        return wr_ == rd_;
    }

    template <typename U = T>
    [[nodiscard]] Expected<void> push(const U& value)
        requires std::is_copy_constructible_v<U>
    {
        std::unique_lock<std::mutex> lk(mutex_);
        std::size_t used = wr_ - rd_;
        if (used >= cap_) {
            return std::unexpected(Error{Result::Busy, std::string_view{"queue full"}});
        }
        buf_[wr_ % cap_].emplace(value);
        ++wr_;
        cv_.notify_one();
        return {};
    }

    [[nodiscard]] Expected<void> push(T&& value) {
        std::unique_lock<std::mutex> lk(mutex_);
        std::size_t used = wr_ - rd_;
        if (used >= cap_) {
            return std::unexpected(Error{Result::Busy, std::string_view{"queue full"}});
        }
        buf_[wr_ % cap_].emplace(std::move(value));
        ++wr_;
        cv_.notify_one();
        return {};
    }

    template <typename... Args>
    [[nodiscard]] Expected<void> emplace(Args&&... args) {
        std::unique_lock<std::mutex> lk(mutex_);
        std::size_t used = wr_ - rd_;
        if (used >= cap_) {
            return std::unexpected(Error{Result::Busy, std::string_view{"queue full"}});
        }
        buf_[wr_ % cap_].emplace(std::forward<Args>(args)...);
        ++wr_;
        cv_.notify_one();
        return {};
    }

    [[nodiscard]] Expected<T> pop() {
        std::unique_lock<std::mutex> lk(mutex_);
        if (wr_ == rd_) {
            return std::unexpected(Error{Result::State, std::string_view{"queue empty"}});
        }
        T val = std::move(*buf_[rd_ % cap_]);
        buf_[rd_ % cap_].reset();
        ++rd_;
        return val;
    }

    // Blocking pop with stop support (optional, not used in tests)
    [[nodiscard]] std::optional<T> waitPop(std::stop_token st = {}) {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, st, [this] { return wr_ != rd_; });
        if (st.stop_requested())
            return std::nullopt;
        if (wr_ == rd_)
            return std::nullopt;
        T val = std::move(*buf_[rd_ % cap_]);
        buf_[rd_ % cap_].reset();
        ++rd_;
        return val;
    }

  private:
    std::size_t cap_;
    std::vector<std::optional<T>> buf_;
    std::size_t wr_{0};
    std::size_t rd_{0};
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
};

} // namespace caudio::utils
