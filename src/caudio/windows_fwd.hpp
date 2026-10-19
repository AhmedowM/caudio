#pragma once

/**
 * @file windows_fwd.hpp
 * @brief Private Windows forward declarations (caudio internal, not installed).
 * @ingroup caudio_utils
 * @details Centralizes Win32 API type aliases, constants, and function
 * declarations so that public headers and multiple .cpp files do not hand-roll
 * them. Guarded by `_WIN32`; skips entirely if `<windows.h>` was already
 * included (`_WINDOWS_` / `_WINDEF_` / `_MINWINDEF_` defined).
 *
 * Never `#include <windows.h>` here -- the point is to avoid forcing it on
 * consumers. .cpp files that need full struct definitions (OVERLAPPED,
 * STARTUPINFOW, PROCESS_INFORMATION) should include `<windows.h>` directly.
 */

#ifndef CAUDIO_WINDOWS_FWD_HPP
#define CAUDIO_WINDOWS_FWD_HPP

#ifdef _WIN32

#if !defined(_WINDOWS_) && !defined(_WINDEF_) && !defined(_MINWINDEF_)

#include <cstdint>

// --- Type aliases ---
using HANDLE = void*;
using HMODULE = void*;
using HRESULT = long;
using DWORD = unsigned long;
using BOOL = int;
using LPCWSTR = const wchar_t*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPDWORD = DWORD*;
using LPSECURITY_ATTRIBUTES = void*;
using LPOVERLAPPED = void*;
using LPCSTR = const char*;
using LPCCH = const char*;
using LPWSTR = wchar_t*;
using UINT = unsigned int;
using FARPROC = long long int (*)();
using PCWSTR = const wchar_t*;

// --- Constants (guarded where windows.h also defines macros) ---
#ifndef INVALID_HANDLE_VALUE
static const HANDLE kInvalidHandleValue = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
#endif
#ifndef GENERIC_READ
static const DWORD kGenericRead = 0x80000000UL;
#endif
#ifndef GENERIC_WRITE
static const DWORD kGenericWrite = 0x40000000UL;
#endif
#ifndef FILE_SHARE_READ
static const DWORD kFileShareRead = 0x00000001UL;
#endif
#ifndef OPEN_EXISTING
static const DWORD kOpenExisting = 3UL;
#endif
#ifndef OPEN_ALWAYS
static const DWORD kOpenAlways = 4UL;
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
static const DWORD kFileAttributeNormal = 0x80UL;
#endif
#ifndef FILE_FLAG_OVERLAPPED
static const DWORD kFileFlagOverlapped = 0x40000000UL;
#endif
#ifndef LOCKFILE_EXCLUSIVE_LOCK
static const DWORD kLockFileExclusiveLock = 0x00000002UL;
#endif
#ifndef LOCKFILE_FAIL_IMMEDIATELY
static const DWORD kLockFileFailImmediately = 0x00000001UL;
#endif
#ifndef SYNCHRONIZE
static const DWORD kSynchronize = 0x00100000UL;
#endif
#ifndef WAIT_TIMEOUT
static const DWORD kWaitTimeout = 258UL;
#endif
#ifndef PIPE_READMODE_BYTE
static const DWORD kPipeReadModeByte = 0x00000000UL;
#endif
static const DWORD kPipeAccessDuplex = 0x00000003UL;
static const DWORD kPipeTypeByte = 0x00000000UL;
static const DWORD kPipeWait = 0x00000000UL;
static const DWORD kPipeUnlimited = 255UL;
static const DWORD kPageReadWrite = 0x04UL;
static const DWORD kFileMapRead = 0x0004UL;
static const DWORD kFileMapWrite = 0x0002UL;
static const DWORD kSddlRevision1 = 1UL;
#ifndef DETACHED_PROCESS
static const DWORD kDetachedProcess = 0x00000008UL;
#endif
#ifndef MAX_PATH
static const DWORD kMaxPath = 260;
#endif
#ifndef ERROR_PIPE_BUSY
static const DWORD kErrorPipeBusy = 231UL;
#endif
#ifndef ERROR_FILE_NOT_FOUND
static const DWORD kErrorFileNotFound = 2UL;
#endif
#ifndef ERROR_BROKEN_PIPE
static const DWORD kErrorBrokenPipe = 109UL;
#endif
#ifndef ERROR_INVALID_PARAMETER
static const DWORD kErrorInvalidParameter = 87UL;
#endif
#ifndef ERROR_SUCCESS
static const DWORD kErrorSuccess = 0UL;
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
static const DWORD kProcessQueryLimitedInformation = 0x1000UL;
#endif
#ifndef FALSE
static const BOOL kFalse = 0;
#endif
#ifndef WINAPI
#define WINAPI __stdcall
#endif

// --- Function declarations ---
extern "C" {
__declspec(dllimport) HANDLE __stdcall CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES,
                                                   DWORD, DWORD, HANDLE);
__declspec(dllimport) HANDLE __stdcall CreateNamedPipeW(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD,
                                                        DWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) BOOL __stdcall ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall WaitNamedPipeW(LPCWSTR, DWORD);
__declspec(dllimport) BOOL __stdcall ConnectNamedPipe(HANDLE, LPVOID);
__declspec(dllimport) BOOL __stdcall DisconnectNamedPipe(HANDLE);
__declspec(dllimport) BOOL __stdcall FlushFileBuffers(HANDLE);
__declspec(dllimport) BOOL __stdcall SetNamedPipeHandleState(HANDLE, LPDWORD, LPDWORD, LPDWORD);
__declspec(dllimport) BOOL __stdcall
ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR, DWORD, LPVOID*, LPDWORD);
__declspec(dllimport) LPVOID __stdcall LocalFree(LPVOID);
__declspec(dllimport) HANDLE __stdcall CreateFileMappingW(HANDLE, LPSECURITY_ATTRIBUTES, DWORD,
                                                          DWORD, DWORD, LPCWSTR);
__declspec(dllimport) HANDLE __stdcall OpenFileMappingW(DWORD, BOOL, LPCWSTR);
__declspec(dllimport) LPVOID __stdcall MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, std::size_t);
__declspec(dllimport) BOOL __stdcall UnmapViewOfFile(LPCVOID);
__declspec(dllimport) HMODULE __stdcall GetModuleHandleA(LPCSTR);
__declspec(dllimport) FARPROC __stdcall GetProcAddress(HMODULE, LPCSTR);
__declspec(dllimport) int __stdcall MultiByteToWideChar(UINT, DWORD, LPCCH, int, LPWSTR, int);
__declspec(dllimport) HANDLE __stdcall GetCurrentThread(void);
__declspec(dllimport) HANDLE __stdcall OpenProcess(DWORD, BOOL, DWORD);
__declspec(dllimport) DWORD __stdcall WaitForSingleObject(HANDLE, DWORD);
__declspec(dllimport) BOOL __stdcall LockFileEx(HANDLE, DWORD, DWORD, DWORD, DWORD, LPOVERLAPPED);
__declspec(dllimport) BOOL __stdcall UnlockFileEx(HANDLE, DWORD, DWORD, DWORD, LPOVERLAPPED);
__declspec(dllimport) DWORD __stdcall GetModuleFileNameW(HANDLE, LPWSTR, DWORD);
__declspec(dllimport) BOOL __stdcall CreateProcessW(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                                                    LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
                                                    LPCWSTR, LPVOID, LPVOID);
} // extern "C"

#endif // !defined(_WINDOWS_) && !defined(_WINDEF_) && !defined(_MINWINDEF_)

#endif // _WIN32

#endif // CAUDIO_WINDOWS_FWD_HPP
