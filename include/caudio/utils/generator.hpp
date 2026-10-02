/**
 * @file generator.hpp
 * @brief Portable coroutine generator.
 * @ingroup caudio_utils
 * @details `std::generator` (C++23 P2502) has no libc++ implementation,
 * so a macOS/brew-LLVM build cannot include `<generator>`. This header
 * provides `caudio::utils::Generator<T>`, a minimal coroutine-based
 * single-pass range supporting `co_yield`/`co_return` and range-for.
 * It uses only `<coroutine>` (available everywhere) and matches the
 * `std::generator` iteration subset used here. Always use this type
 * instead of `std::generator` directly.
 */
#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace caudio::utils {

/**
 * @brief Lazy single-pass coroutine range.
 * @ingroup caudio_utils
 * @tparam T Yielded value type.
 */
template <typename T>
class Generator {
  public:
    /// @brief Coroutine promise. @ingroup caudio_utils
    struct promise_type {
        std::optional<T> value_;
        std::exception_ptr ex_;

        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept {
            return {};
        }
        std::suspend_always final_suspend() noexcept {
            return {};
        }
        std::suspend_always yield_value(T v) {
            value_.emplace(std::move(v));
            return {};
        }
        void return_void() {}
        void unhandled_exception() {
            ex_ = std::current_exception();
        }
    };

    using Handle = std::coroutine_handle<promise_type>;

    /// @brief Wraps a coroutine handle. @ingroup caudio_utils
    /// @param h Live promise handle.
    explicit Generator(Handle h) noexcept : handle_(h) {}
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;
    /// @brief Transfers ownership. @ingroup caudio_utils
    /// @param o Source; left empty.
    Generator(Generator&& o) noexcept : handle_(std::exchange(o.handle_, {})) {}
    Generator& operator=(Generator&&) = delete;
    /// @brief Destroys the coroutine. @ingroup caudio_utils
    ~Generator() {
        if (handle_)
            handle_.destroy();
    }

    /// @brief End sentinel. @ingroup caudio_utils
    struct sentinel {};

    /// @brief Input iterator over yielded values. @ingroup caudio_utils
    struct iterator {
        Handle handle_ = {};

        iterator& operator++() {
            handle_.promise().value_.reset();
            handle_.resume();
            if (handle_.promise().ex_)
                std::rethrow_exception(handle_.promise().ex_);
            return *this;
        }
        T& operator*() const {
            return *handle_.promise().value_;
        }
        bool operator==(sentinel) const {
            return handle_.done();
        }
    };

    /**
     * @brief Starts iteration (resumes to first yield).
     * @ingroup caudio_utils
     * @return Iterator positioned at the first value (or end).
     */
    iterator begin() {
        if (handle_) {
            handle_.resume();
            if (handle_.promise().ex_)
                std::rethrow_exception(handle_.promise().ex_);
        }
        return iterator{handle_};
    }

    /// @brief End of range. @ingroup caudio_utils
    /// @return Sentinel compared against `done()`.
    sentinel end() const {
        return {};
    }

  private:
    Handle handle_ = {};
};

} // namespace caudio::utils
