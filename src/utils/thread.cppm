module;
#if defined(_WIN32) || defined(_WIN64)
// No windows.h here to avoid intrin conflict; Win32 decls go after export module
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif
#include <chrono>
#include <cstring>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

/**
 * @file thread.cppm
 * @brief Thread naming and sleep helpers (cross-platform).
 * @ingroup caudio_utils
 */

export module caudio.utils:thread;

import :result;
import :error;

#if defined(_WIN32) || defined(_WIN64)
using HANDLE = void*;
using HMODULE = void*;
using HRESULT = long;
using PCWSTR = const wchar_t*;
using LPCSTR = const char*;
using LPCCH = const char*;
using LPWSTR = wchar_t*;
using UINT = unsigned int;
using DWORD = unsigned long;
using FARPROC = long long int (*)();
inline constexpr UINT kCpUtf8 = 65001;
#define WINAPI __stdcall
#endif

#if defined(_WIN32) || defined(_WIN64)
extern "C" {
__declspec(dllimport) HMODULE __stdcall GetModuleHandleA(LPCSTR);
__declspec(dllimport) FARPROC __stdcall GetProcAddress(HMODULE, LPCSTR);
__declspec(dllimport) int __stdcall MultiByteToWideChar(UINT, DWORD, LPCCH, int, LPWSTR, int);
__declspec(dllimport) HANDLE __stdcall GetCurrentThread(void);
}
#endif

namespace caudio::utils::detail {
#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief Sets native handle thread description via SetThreadDescription (Win10+).
 * @param nativeHandle HANDLE of thread (void*).
 * @param name UTF-8 view converted to wide string.
 * @return Expected<void> always success on Windows (errors ignored for compat).
 * @details Dynamically resolves SetThreadDescription from kernel32.dll; falls
 * back to success if unavailable. Converts UTF-8 via MultiByteToWideChar.
 */
inline Expected<void> setNativeHandleName(void* nativeHandle, std::string_view name) noexcept {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32) {
        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        auto pSetDesc =
            reinterpret_cast<SetThreadDescriptionFn>(GetProcAddress(k32, "SetThreadDescription"));
#pragma GCC diagnostic pop
        if (pSetDesc) {
            int wlen = MultiByteToWideChar(kCpUtf8, 0, name.data(), static_cast<int>(name.size()),
                                           nullptr, 0);
            if (wlen > 0) {
                std::wstring wbuf(static_cast<std::size_t>(wlen), L'\0');
                MultiByteToWideChar(kCpUtf8, 0, name.data(), static_cast<int>(name.size()),
                                    wbuf.data(), wlen);
                HRESULT hr = pSetDesc(reinterpret_cast<HANDLE>(nativeHandle), wbuf.c_str());
                (void)hr;
                return {};
            }
        }
    }
    return {};
}
/**
 * @brief Sets current thread name on Windows.
 * @param name UTF-8 name view.
 * @return Expected<void> always success (compat).
 */
inline Expected<void> setCurrentThreadNameImpl(std::string_view name) noexcept {
    return setNativeHandleName(GetCurrentThread(), name);
}
#else
/**
 * @brief Truncates name to 15 chars for pthread limit.
 * @param s Input view.
 * @return View of first 15 bytes (pthread limit is 16 inc. NUL).
 */
constexpr std::string_view truncate15(std::string_view s) noexcept {
    return s.substr(0, 15);
}
/**
 * @brief Sets pthread name with truncation and NUL termination.
 * @param th pthread_t handle.
 * @param name View truncated to 15 chars.
 * @return 0 on success, errno-style error otherwise.
 * @details Copies truncated view into 16-byte buffer with NUL terminator,
 * calls pthread_setname_np.
 */
inline int setPthreadName(pthread_t th, std::string_view name) noexcept {
    std::string_view t = truncate15(name);
    char buf[16]{};
    if (!t.empty())
        std::memcpy(buf, t.data(), t.size());
    buf[t.size()] = '\0';
    return pthread_setname_np(th, buf);
}
#endif
} // namespace caudio::utils::detail

export namespace caudio::utils {

/**
 * @brief Sleeps for a chrono duration.
 * @ingroup caudio_utils
 * @tparam Rep Duration rep type.
 * @tparam Period Duration period type.
 * @param d Duration to sleep.
 * @details Thin wrapper around `std::this_thread::sleep_for`; noexcept.
 */
 // convenience wrapper
template <typename Rep, typename Period>
inline void sleepFor(std::chrono::duration<Rep, Period> d) noexcept {
    std::this_thread::sleep_for(d);
}

/**
 * @brief Sleeps for a number of milliseconds.
 * @ingroup caudio_utils
 * @param ms Milliseconds to sleep.
 * @details Delegates to sleepFor(std::chrono::milliseconds).
 */
 // convenience wrapper
inline void sleepForMs(std::uint32_t ms) noexcept {
    sleepFor(std::chrono::milliseconds(ms));
}

/**
 * @brief Sets the current thread's name.
 * @ingroup caudio_utils
 * @param name Non-empty name view (UTF-8 on Windows).
 * @return Expected<void> success, or error with codes:
 * - `InvalidArg` if `name` empty,
 * - `Unsupported` on macOS/Linux if `pthread_setname_np` fails or platform unsupported,
 * - `Unsupported` with "setThreadName not supported" on unknown POSIX,
 * - Always success on Windows (errors ignored for compat).
 * @details Platform:
 * - Windows: uses `SetThreadDescription` via dynamic `GetProcAddress`; if
 *   unavailable, returns success (no-op for compat).
 * - macOS: `pthread_setname_np` for current thread only; fails with
 *   `Unsupported` if non-zero return.
 * - Linux/glibc: `pthread_setname_np(pthread_self(), truncated15)`; truncates
 *   to 15 chars + NUL.
 * @see setThreadName(std::jthread&, std::string_view)
 * @see setThreadName(std::thread&, std::string_view)
 */
[[nodiscard]] inline Expected<void> setThreadName(std::string_view name) noexcept {
    if (name.empty()) {
        return std::unexpected(Error{StatusCode::InvalidArg, "empty name"});
    }
#if defined(_WIN32) || defined(_WIN64)
    return detail::setCurrentThreadNameImpl(name);
#else
#if defined(__APPLE__) && defined(__MACH__)
    std::string_view t = detail::truncate15(name);
    char buf[16]{};
    if (!t.empty())
        std::memcpy(buf, t.data(), t.size());
    buf[t.size()] = '\0';
    int rc = pthread_setname_np(buf);
    if (rc != 0) {
        return std::unexpected(Error{StatusCode::Unsupported, "pthread_setname_np failed"});
    }
    return {};
#elif defined(__linux__) || defined(_GNU_SOURCE) || defined(__GLIBC__)
    int rc = detail::setPthreadName(pthread_self(), name);
    if (rc != 0) {
        return std::unexpected(Error{StatusCode::Unsupported, "pthread_setname_np failed"});
    }
    return {};
#else
    (void)name;
    return std::unexpected(Error{StatusCode::Unsupported, "setThreadName not supported"});
#endif
#endif
}

/**
 * @brief Sets a `std::jthread`'s name by native handle.
 * @ingroup caudio_utils
 * @param jt Joinable jthread whose name to set.
 * @param name Non-empty name view.
 * @return Expected<void> success or error:
 * - `State` with "thread not joinable" if !jt.joinable(),
 * - `InvalidArg` if name empty,
 * - `Unsupported` if platform does not support naming this handle
 *   (macOS jthread unsupported, unknown POSIX),
 * - `Unsupported` with "pthread_setname_np failed" on Linux failure,
 * - Always success on Windows (via SetThreadDescription).
 * @details Windows reinterprets `native_handle()` via HANDLE; Linux uses
 * `pthread_setname_np(th, truncated15)`.
 * @see setThreadName(std::string_view)
 */
[[nodiscard]] inline Expected<void> setThreadName(std::jthread& jt,
                                                  std::string_view name) noexcept {
    if (!jt.joinable()) {
        return std::unexpected(Error{StatusCode::State, "thread not joinable"});
    }
    if (name.empty()) {
        return std::unexpected(Error{StatusCode::InvalidArg, "empty name"});
    }
#if defined(_WIN32) || defined(_WIN64)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(jt.native_handle()));
    return detail::setNativeHandleName(h, name);
#else
    pthread_t th = jt.native_handle();
#if defined(__APPLE__) && defined(__MACH__)
    // macOS pthread_setname_np only supports naming the current thread (no handle arg).
    // std::jthread native_handle is not the current thread, so we cannot name it.
    (void)th;
    (void)name;
    return std::unexpected(
        Error{StatusCode::Unsupported, "setThreadName with jthread not supported on macOS"});
#elif defined(__linux__)
    int rc = detail::setPthreadName(th, name);
    if (rc != 0) {
        return std::unexpected(Error{StatusCode::Unsupported, "pthread_setname_np failed"});
    }
    return {};
#else
    (void)th;
    (void)name;
    return std::unexpected(Error{StatusCode::Unsupported, "setThreadName not supported"});
#endif
#endif
}

/**
 * @brief Sets a `std::thread`'s name by native handle.
 * @ingroup caudio_utils
 * @param t Joinable thread whose name to set.
 * @param name Non-empty name view.
 * @return Expected<void> success or error:
 * - `State` if !t.joinable(),
 * - `InvalidArg` if name empty,
 * - `Unsupported` on non-Linux POSIX or `pthread_setname_np` failure,
 * - Always success on Windows.
 * @details Linux path only; other POSIX returns Unsupported (macOS falls
 * through to generic unsupported). Name truncated to 15 chars on Linux.
 * @see setThreadName(std::jthread&, std::string_view)
 */
 // Convenience for std::thread as well
[[nodiscard]] inline Expected<void> setThreadName(std::thread& t, std::string_view name) noexcept {
    if (!t.joinable()) {
        return std::unexpected(Error{StatusCode::State, "thread not joinable"});
    }
    if (name.empty()) {
        return std::unexpected(Error{StatusCode::InvalidArg, "empty name"});
    }
#if defined(_WIN32) || defined(_WIN64)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(t.native_handle()));
    return detail::setNativeHandleName(h, name);
#else
#if defined(__linux__) && !defined(__APPLE__)
    pthread_t th = t.native_handle();
    int rc = detail::setPthreadName(th, name);
    if (rc != 0)
        return std::unexpected(Error{StatusCode::Unsupported, "pthread_setname_np failed"});
    return {};
#else
    (void)t;
    (void)name;
    return std::unexpected(Error{StatusCode::Unsupported, "not supported"});
#endif
#endif
}

} // namespace caudio::utils
