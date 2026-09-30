#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "service_status.hpp"

#include <blake3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#else
#include <process.h>
// Avoid including windows.h -- causes HMODULE conflict with caudio::utils
// Provide minimal forward declarations for needed APIs
using HANDLE = void*;
using DWORD = unsigned long;
using BOOL = int;
using LPCWSTR = const wchar_t*;
using LPVOID = void*;
using LPDWORD = DWORD*;
using LPOVERLAPPED = void*;
using LPSECURITY_ATTRIBUTES = void*;
inline constexpr DWORD kGenericReadW = 0x80000000UL;
inline constexpr DWORD kGenericWriteW = 0x40000000UL;
inline constexpr DWORD kFileShareReadW = 0x00000001UL;
inline constexpr DWORD kOpenAlwaysW = 4UL;
inline constexpr DWORD kFileAttributeNormalW = 0x80UL;
inline constexpr DWORD kFileFlagOverlappedW = 0x40000000UL;
inline constexpr DWORD kLockFileExclusiveLockW = 0x00000002UL;
inline constexpr DWORD kLockFileFailImmediatelyW = 0x00000001UL;
inline constexpr DWORD kSynchronizeW = 0x00100000UL;
inline constexpr DWORD kWaitTimeoutW = 258UL;
inline constexpr DWORD kOpenExistingW = 3UL;
inline constexpr BOOL kFalseW = 0;
inline const HANDLE kInvalidHandleValueW = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
// winbase.h mirror (named members: anonymous struct/union is a GNU
// extension Clang rejects under -Wpedantic; the struct is currently
// unused -- LPOVERLAPPED above covers the decls).
struct OVERLAPPED {
    void* Internal{nullptr};
    void* InternalHigh{nullptr};
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        } offsetPart;
        void* Pointer;
    } offsetOrPointer;
    HANDLE hEvent{nullptr};
};
extern "C" {
__declspec(dllimport) HANDLE __stdcall CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                                   DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) BOOL __stdcall LockFileEx(HANDLE, DWORD, DWORD, DWORD, DWORD, LPOVERLAPPED);
__declspec(dllimport) BOOL __stdcall UnlockFileEx(HANDLE, DWORD, DWORD, DWORD, LPOVERLAPPED);
__declspec(dllimport) HANDLE __stdcall OpenProcess(DWORD, BOOL, DWORD);
__declspec(dllimport) DWORD __stdcall WaitForSingleObject(HANDLE, DWORD);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall WaitNamedPipeW(LPCWSTR, DWORD);
}
#ifndef GENERIC_READ
#define GENERIC_READ kGenericReadW
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE kGenericWriteW
#endif
#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ kFileShareReadW
#endif
#ifndef OPEN_ALWAYS
#define OPEN_ALWAYS kOpenAlwaysW
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL kFileAttributeNormalW
#endif
#ifndef FILE_FLAG_OVERLAPPED
#define FILE_FLAG_OVERLAPPED kFileFlagOverlappedW
#endif
#ifndef LOCKFILE_EXCLUSIVE_LOCK
#define LOCKFILE_EXCLUSIVE_LOCK kLockFileExclusiveLockW
#endif
#ifndef LOCKFILE_FAIL_IMMEDIATELY
#define LOCKFILE_FAIL_IMMEDIATELY kLockFileFailImmediatelyW
#endif
#ifndef SYNCHRONIZE
#define SYNCHRONIZE kSynchronizeW
#endif
#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT kWaitTimeoutW
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE kInvalidHandleValueW
#endif
#ifndef OPEN_EXISTING
#define OPEN_EXISTING kOpenExistingW
#endif
#ifndef FALSE
#define FALSE kFalseW
#endif
#endif

#include <caudio/config.hpp>
#include <caudio/db.hpp>
#include <caudio/engine.hpp>
#include <caudio/ipc.hpp>
#include <caudio/player.hpp>
#include <caudio/utils.hpp>

namespace caudio::service::detail {
std::expected<caudio::ipc::Status, caudio::utils::Error> buildStatus(caudio::engine::Engine& eng,
                                                                     caudio::db::Database& db) {
    caudio::ipc::Status s{};
    s.version = std::string(caudio::versionFull);
    s.state = eng.state();
    s.pos = eng.position();
    s.dur = eng.duration();
    s.vol = eng.volume();
    s.muted = false;
    s.shuffle = eng.shuffle();
    s.repeat = eng.repeat();
    s.track_id = eng.currentTrackId();
    if (s.track_id != 0) {
        auto tr = db.getTrack(s.track_id);
        if (tr) {
            s.title = tr->title;
            s.artist = tr->artist;
            s.path = tr->path;
        }
    }
    try {
        int64_t activeQ = eng.activeQueueId();
        auto items = db.queueList(activeQ);
        if (items) {
            s.q_size = items->size();
            s.q_idx = 0;
            if (s.track_id != 0 && !items->empty()) {
                for (std::size_t i = 0; i < items->size(); ++i) {
                    if ((*items)[i].track_id == s.track_id) {
                        s.q_idx = i;
                        break;
                    }
                }
            }
        }
    } catch (...) {
    }
    return s;
}

} // namespace caudio::service::detail
