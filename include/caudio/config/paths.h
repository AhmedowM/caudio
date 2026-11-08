#ifndef CAUDIO_CONFIG_PATHS_H
#define CAUDIO_CONFIG_PATHS_H

/**
 * @file caudio/config/paths.h
 * @brief Canonical runtime paths and path derivation.
 *
 * Mirrors `kanariv::config::RuntimePaths` and the path helpers in
 * `kanariv::config` / `kanariv::config::detail`. Every path is UTF-8 with '/'
 * separators; Windows backslashes are normalized at the platform boundary.
 *
 * Runtime paths let a host (for example an Android embedding) supply every
 * application-private location up front instead of relying on desktop
 * environment discovery. Empty fields mean "not supplied by the host".
 *
 * Derived names use the `caudio-` prefix:
 * - `caudio-<hex>.sock` (POSIX) or `\\.\pipe\caudio-<hex>` (Windows)
 * - `caudio-<hex>.pid`, `caudio-<hex>.lock`
 * - `caudio.log`
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Application-private directories and an optional transport endpoint.
 *
 * Field meanings:
 * - `database`: SQLite file (not a directory).
 * - `configuration`: configuration file or its parent, as agreed by host and
 *   caller.
 * - `logs`: directory for log files.
 * - `cache`: disposable cache directory.
 * - `imported_media`: directory for app-private imported media copies.
 * - `socket`: explicit local socket/named-pipe endpoint, when used.
 *
 * String members are `cx_string *` handles. nullptr and the empty string are
 * both treated as "not supplied"; lifecycle helpers normalize to nullptr.
 */
typedef struct ca_runtime_paths {
    cx_string *database;
    cx_string *configuration;
    cx_string *logs;
    cx_string *cache;
    cx_string *imported_media;
    cx_string *socket;
} ca_runtime_paths;

/** @brief Initialize a path set to all-empty. */
CAUDIO_API ca_status ca_runtime_paths_init(ca_runtime_paths *paths,
                                           const cx_allocator *alloc);

/** @brief Deep-copy @p src into @p dst. */
CAUDIO_API ca_status ca_runtime_paths_copy(ca_runtime_paths *dst,
                                           const ca_runtime_paths *src,
                                           const cx_allocator *alloc);

/** @brief Move-assign @p src into @p dst, destroying @p dst's contents first. */
CAUDIO_API void ca_runtime_paths_move(ca_runtime_paths *dst,
                                      ca_runtime_paths *src);

/** @brief Release every owned string and zero the structure. */
CAUDIO_API void ca_runtime_paths_destroy(ca_runtime_paths *paths);

/** @brief Return true when every field is empty (nullptr or zero length). */
CAUDIO_API bool ca_runtime_paths_empty(const ca_runtime_paths *paths);

/**
 * @brief Create the directories implied by @p paths.
 *
 * Creates the parent directory of `database` and `configuration`, and the
 * `logs`, `cache`, and `imported_media` directories themselves. Empty fields
 * are skipped and existing directories are accepted. An all-empty set is a
 * successful no-op.
 *
 * @return CA_OK, or an I/O status with @p err set.
 */
CAUDIO_API ca_status
ca_runtime_paths_ensure_directories(const ca_runtime_paths *paths,
                                    ca_error *err);

/**
 * @brief Write the default database path into @p out.
 *
 * Windows: `%LOCALAPPDATA%/caudio/library.db`. POSIX: `$XDG_DATA_HOME/caudio`
 * or `~/.local/share/caudio`, falling back to the temporary directory.
 */
CAUDIO_API ca_status ca_paths_default_db_path(cx_string **out,
                                              const cx_allocator *alloc,
                                              ca_error *err);

/**
 * @brief Write the default configuration path into @p out.
 *
 * Windows: `%LOCALAPPDATA%/caudio/config.json`. POSIX:
 * `$XDG_CONFIG_HOME/caudio/config.json` or `~/.config/caudio/config.json`,
 * falling back to the temporary directory.
 */
CAUDIO_API ca_status ca_paths_default_config_path(cx_string **out,
                                                  const cx_allocator *alloc,
                                                  ca_error *err);

/**
 * @brief Write the legacy Windows configuration path into @p out.
 *
 * Windows: `%USERPROFILE%/.config/caudio/config.json`. On other platforms this
 * is an empty string.
 */
CAUDIO_API ca_status ca_paths_legacy_config_path(cx_string **out,
                                                 const cx_allocator *alloc);

/**
 * @brief Compute the stable 8-hex-digit identity of @p db_path.
 *
 * The hash is FNV-1a over the identity-normalized path (see
 * ca_path_identity_normalize), folded to 32 bits. The output buffer must hold
 * at least 9 bytes (8 hex digits plus NUL).
 */
CAUDIO_API void ca_paths_hex8_for_db(const char *db_path, char out[9]);

/**
 * @brief Derive the local IPC endpoint for @p db_path.
 *
 * POSIX: `$XDG_RUNTIME_DIR/caudio/caudio-<hex>.sock` (with the same fallbacks
 * as the data directory). Windows: `\\.\pipe\caudio-<hex>`.
 */
CAUDIO_API ca_status ca_paths_socket_for(const char *db_path,
                                         cx_string **out,
                                         const cx_allocator *alloc,
                                         ca_error *err);

/** @brief Derive `caudio-<hex>.pid` next to the socket. */
CAUDIO_API ca_status ca_paths_pid_for(const char *db_path, cx_string **out,
                                      const cx_allocator *alloc,
                                      ca_error *err);

/** @brief Derive `caudio-<hex>.lock` next to the socket. */
CAUDIO_API ca_status ca_paths_lock_for(const char *db_path, cx_string **out,
                                       const cx_allocator *alloc,
                                       ca_error *err);

/**
 * @brief Derive the log path for @p db_path.
 *
 * Uses the database's parent directory and `caudio.log`, falling back to the
 * temporary directory when the database has no parent.
 */
CAUDIO_API ca_status ca_paths_log_for(const char *db_path, cx_string **out,
                                      const cx_allocator *alloc,
                                      ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_CONFIG_PATHS_H */
