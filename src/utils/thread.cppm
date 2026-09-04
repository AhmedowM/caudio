module;
#if defined(_WIN32) || defined(_WIN64)
// No windows.h here to avoid intrin conflict; Win32 decls go after export module
#else
#include <pthread.h>
#include <time.h>
#include <errno.h>
#endif
#include <thread>
#include <chrono>
#include <string>
#include <string_view>
#include <expected>
#include <utility>
#include <functional>

export module caudio.utils:thread;

import :result;
import :error;

#if defined(_WIN32) || defined(_WIN64)
extern "C" {
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
  __declspec(dllimport) HMODULE __stdcall GetModuleHandleA(LPCSTR);
  __declspec(dllimport) FARPROC __stdcall GetProcAddress(HMODULE, LPCSTR);
  __declspec(dllimport) int __stdcall MultiByteToWideChar(UINT, DWORD, LPCCH, int, LPWSTR, int);
  __declspec(dllimport) HANDLE __stdcall GetCurrentThread(void);
}
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#ifndef WINAPI
#define WINAPI __stdcall
#endif
#endif

export namespace caudio::utils {

inline void sleepFor(std::chrono::milliseconds ms) noexcept {
  std::this_thread::sleep_for(ms);
}

template <typename Rep, typename Period>
inline void sleepFor(std::chrono::duration<Rep, Period> d) noexcept {
  std::this_thread::sleep_for(d);
}

inline void sleepForMs(std::uint32_t ms) noexcept {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

[[nodiscard]] inline Expected<void> setThreadName(std::string_view name) noexcept {
  if (name.empty()) {
    // empty name is allowed on Windows, but treat as InvalidArg? C returned Ok even for empty.
    // Allow empty -> Ok
  }
  if (name.data() == nullptr && !name.empty()) {
    return std::unexpected(Error{Result::InvalidArg, "null name"});
  }
#if defined(_WIN32) || defined(_WIN64)
  // Use SetThreadDescription if available (Windows 10 1607+)
  HMODULE k32 = GetModuleHandleA("kernel32.dll");
  if (k32) {
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    auto pSetDesc = (SetThreadDescriptionFn)GetProcAddress(k32, "SetThreadDescription");
#pragma GCC diagnostic pop
    if (pSetDesc) {
      int wlen = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
      // Need null terminator
      std::wstring wbuf;
      wbuf.resize(static_cast<std::size_t>(wlen + 1));
      MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wbuf.data(), wlen);
      wbuf[wlen] = L'\0';
      // truncate to wlen chars
      wbuf.resize(static_cast<std::size_t>(wlen));
      // Need null-terminated buffer for API
      std::wstring wnull = std::wstring(name.size() * 2 + 4, L'\0');
      int wlen2 = MultiByteToWideChar(CP_UTF8, 0, std::string(name).c_str(), -1, wnull.data(), static_cast<int>(wnull.size()));
      if (wlen2 > 0) {
        wnull.resize(static_cast<std::size_t>(wlen2 - 1));
        // API expects null-terminated; we pass wnull.c_str()
        // Use GetCurrentThread
        HRESULT hr = pSetDesc(GetCurrentThread(), wnull.c_str());
        (void)hr;
        return {};
      }
    }
  }
  return {};
#else
  // POSIX
#if defined(__APPLE__) && defined(__MACH__)
  // macOS: pthread_setname_np takes only name (16 char limit including NUL)
  std::string copy(name);
  if (copy.size() > 15) copy.resize(15);
  int rc = pthread_setname_np(copy.c_str());
  if (rc != 0) {
    return std::unexpected(Error{Result::Unsupported, "pthread_setname_np failed"});
  }
  return {};
#elif defined(__linux__)
  std::string copy(name);
  if (copy.size() > 15) copy.resize(15);
  int rc = pthread_setname_np(pthread_self(), copy.c_str());
  if (rc != 0) {
    return std::unexpected(Error{Result::Unsupported, "pthread_setname_np failed"});
  }
  return {};
#else
#if defined(_GNU_SOURCE) || defined(__GLIBC__)
  std::string copy(name);
  if (copy.size() > 15) copy.resize(15);
  int rc = pthread_setname_np(pthread_self(), copy.c_str());
  if (rc != 0) {
    return std::unexpected(Error{Result::Unsupported, "pthread_setname_np failed"});
  }
  return {};
#else
  (void)name;
  return std::unexpected(Error{Result::Unsupported, "setThreadName not supported"});
#endif
#endif
#endif
}

[[nodiscard]] inline Expected<void> setThreadName(std::jthread& jt, std::string_view name) noexcept {
  if (!jt.joinable()) {
    return std::unexpected(Error{Result::State, "thread not joinable"});
  }
  if (name.data() == nullptr && !name.empty()) {
    return std::unexpected(Error{Result::InvalidArg, "null name"});
  }
#if defined(_WIN32) || defined(_WIN64)
  // std::jthread::native_handle() on MinGW may be HANDLE or integer; use C-style cast via uintptr_t
  HANDLE h = (HANDLE)(uintptr_t)jt.native_handle();
  HMODULE k32 = GetModuleHandleA("kernel32.dll");
  if (k32) {
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    auto pSetDesc = (SetThreadDescriptionFn)GetProcAddress(k32, "SetThreadDescription");
#pragma GCC diagnostic pop
    if (pSetDesc) {
      std::string s(name);
      int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
      if (wlen > 0) {
        std::wstring wbuf(static_cast<std::size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wbuf.data(), wlen);
        // wbuf includes null terminator; pass c_str
        HRESULT hr = pSetDesc(h, wbuf.c_str());
        (void)hr;
        return {};
      }
    }
  }
  return {};
#else
  pthread_t th = jt.native_handle();
  // pthread_setname_np with pthread_t variant (Linux has pthread_setname_np(pthread_t, const char*))
  // On macOS, pthread_setname_np does not take thread arg; fallback to Unsupported
#if defined(__APPLE__) && defined(__MACH__)
  (void)th;
  (void)name;
  return std::unexpected(Error{Result::Unsupported, "setThreadName with jthread not supported on macOS"});
#elif defined(__linux__)
  std::string copy(name);
  if (copy.size() > 15) copy.resize(15);
  int rc = pthread_setname_np(th, copy.c_str());
  if (rc != 0) {
    return std::unexpected(Error{Result::Unsupported, "pthread_setname_np failed"});
  }
  return {};
#else
  (void)th;
  (void)name;
  return std::unexpected(Error{Result::Unsupported, "setThreadName not supported"});
#endif
#endif
}

// Convenience for std::thread as well
[[nodiscard]] inline Expected<void> setThreadName(std::thread& t, std::string_view name) noexcept {
  if (!t.joinable()) {
    return std::unexpected(Error{Result::State, "thread not joinable"});
  }
#if defined(_WIN32) || defined(_WIN64)
  HANDLE h = (HANDLE)(uintptr_t)t.native_handle();
  HMODULE k32 = GetModuleHandleA("kernel32.dll");
  if (k32) {
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    auto pSetDesc = (SetThreadDescriptionFn)GetProcAddress(k32, "SetThreadDescription");
#pragma GCC diagnostic pop
    if (pSetDesc) {
      std::string s(name);
      int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
      if (wlen > 0) {
        std::wstring wbuf(static_cast<std::size_t>(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wbuf.data(), wlen);
        HRESULT hr = pSetDesc(h, wbuf.c_str());
        (void)hr;
        return {};
      }
    }
  }
  return {};
#else
  #if defined(__linux__) && !defined(__APPLE__)
  pthread_t th = t.native_handle();
  std::string copy(name);
  if (copy.size() > 15) copy.resize(15);
  int rc = pthread_setname_np(th, copy.c_str());
  if (rc != 0) return std::unexpected(Error{Result::Unsupported, "pthread_setname_np failed"});
  return {};
  #else
  (void)t; (void)name;
  return std::unexpected(Error{Result::Unsupported, "not supported"});
  #endif
#endif
}

} // namespace caudio::utils
