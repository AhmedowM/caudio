#ifndef CAUDIO_RUNTIME_FS_H
#define CAUDIO_RUNTIME_FS_H

/**
 * @file caudio/runtime/fs.h
 * @brief Filesystem operations.
 *
 * Thin POSIX/Win32 wrapper around file and directory operations. All paths
 * are UTF-8 encoded. On Windows, UTF-8 is converted to UTF-16 for the native
 * API calls.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open a file.
 *
 * @param path      UTF-8 path to open.
 * @param flags     Combination of CA_FS_READ, CA_FS_WRITE, CA_FS_APPEND,
 *                  CA_FS_CREATE, CA_FS_TRUNCATE.
 * @param err       Optional error record; zeroed on success, set on failure.
 * @return          Opaque file handle, or CA_FS_INVALID on failure.
 */
typedef struct ca_file* ca_file_t;

constexpr ca_file_t CA_FS_INVALID = nullptr;

constexpr uint32_t CA_FS_READ = 1u << 0;
constexpr uint32_t CA_FS_WRITE = 1u << 1;
constexpr uint32_t CA_FS_APPEND = 1u << 2;
constexpr uint32_t CA_FS_CREATE = 1u << 3;
constexpr uint32_t CA_FS_TRUNCATE = 1u << 4;

CAUDIO_API ca_file_t ca_fs_open(const char* path, uint32_t flags, ca_error* err);

/**
 * @brief Read up to @p size bytes from @p file into @p buf.
 *
 * @return Bytes read, or 0 on EOF. On error, @p err is set and (size_t)-1 is
 *         returned.
 */
CAUDIO_API size_t ca_fs_read(ca_file_t file, void* buf, size_t size, ca_error* err);

/**
 * @brief Write @p size bytes from @p buf to @p file.
 *
 * @return Bytes written, or (size_t)-1 on error with @p err set.
 */
CAUDIO_API size_t ca_fs_write(ca_file_t file, const void* buf, size_t size, ca_error* err);

/**
 * @brief Close @p file. Safe to call on CA_FS_INVALID (no-op).
 */
CAUDIO_API void ca_fs_close(ca_file_t file);

/**
 * @brief Get the size of @p file in bytes.
 *
 * @return Size in bytes, or (uint64_t)-1 on error with @p err set.
 */
CAUDIO_API uint64_t ca_fs_size(ca_file_t file, ca_error* err);

/**
 * @brief Check whether @p path exists.
 *
 * @return true if the path exists, false otherwise (or on error with @p err
 *         set).
 */
CAUDIO_API bool ca_fs_exists(const char* path, ca_error* err);

/**
 * @brief Create a directory (including parents).
 *
 * @return CA_OK on success, or an error status with @p err set.
 */
CAUDIO_API ca_status ca_fs_mkdirs(const char* path, ca_error* err);

/**
 * @brief Remove a file or empty directory.
 *
 * @return CA_OK on success, or an error status with @p err set.
 */
CAUDIO_API ca_status ca_fs_remove(const char* path, ca_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_FS_H */
