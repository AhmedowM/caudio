#ifndef CAUDIO_CONFIG_CONFIG_H
#define CAUDIO_CONFIG_CONFIG_H

/**
 * @file caudio/config/config.h
 * @brief Configuration record, load/save, and raw key/value access.
 *
 * Mirrors `kanariv::config`. The JSON file uses the same canonical field names
 * and defaults. Loading is tolerant: missing or wrongly typed keys fall back to
 * defaults, legacy `db_path`/`log_level` keys are accepted, and hand-written
 * log level names ("info") are understood. Saving always writes the canonical
 * form with 2-space indentation.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <caudio/config/paths.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/vector.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Application configuration.
 *
 * Maps 1:1 to the `Config` value in the C++ implementation.
 */
typedef struct ca_config {
    cx_string *db_path;        /**< SQLite database path. */
    cx_string *config_path;    /**< Config file path. */
    cx_string *device;         /**< Audio device id; "auto" for default. */
    int32_t log_level;         /**< 0=trace, 1=debug, 2=info, 3=warn, 4=error. */
    cx_string *socket_path;    /**< Optional explicit socket path. */
    bool auto_resume_on_play;  /**< Resume persisted track/position on play. */
    bool clear_stops_playback; /**< Clearing the queue also stops playback. */
    cx_string *repeat_default; /**< "off", "one", or "all". */
    bool autostart;            /**< Auto-start when adding to an empty queue. */
} ca_config;

/** @brief Initialize @p config with canonical defaults. */
CAUDIO_API ca_status ca_config_init(ca_config *config,
                                    const cx_allocator *alloc);

/** @brief Deep-copy @p src into @p dst. */
CAUDIO_API ca_status ca_config_copy(ca_config *dst, const ca_config *src,
                                    const cx_allocator *alloc);

/** @brief Move-assign @p src into @p dst, destroying @p dst first. */
CAUDIO_API void ca_config_move(ca_config *dst, ca_config *src);

/** @brief Release owned strings and zero the record. */
CAUDIO_API void ca_config_destroy(ca_config *config);

/**
 * @brief Load configuration from @p path, merging over defaults.
 *
 * @param out   Receives the loaded configuration on success.
 * @param path  Config file path; NULL or empty means the default path (with
 *              the legacy Windows path as a fallback when the default file
 *              does not exist).
 * @param alloc Allocator for the loaded strings; NULL means default.
 * @param err   Optional error record.
 * @return CA_OK when the file is missing (defaults are returned), CA_JSON_ERROR
 *         for malformed JSON, CA_INVALID_ARGUMENT when the root is not an
 *         object, or an I/O status.
 */
CAUDIO_API ca_status ca_config_load(ca_config *out, const char *path,
                                    const cx_allocator *alloc, ca_error *err);

/**
 * @brief Save @p config to its `config_path`.
 *
 * Creates the parent directory when needed and writes 2-space pretty JSON.
 * `socketPath` is written only when non-empty. The canonical member order is
 * `dbPath`, `device`, `logLevel`, `autoResumeOnPlay`, `clearStopsPlayback`,
 * `repeatDefault`, `autostart`, then `socketPath`.
 */
CAUDIO_API ca_status ca_config_save(const ca_config *config,
                                    const cx_allocator *alloc, ca_error *err);

/**
 * @brief One raw key/value pair.
 *
 * Values are strings: JSON strings are returned unquoted, `null` as "null",
 * and every other value as compact JSON.
 */
typedef struct ca_config_entry {
    cx_string *key;
    cx_string *value;
} ca_config_entry;

/** @brief Initialize an empty entry. */
CAUDIO_API ca_status ca_config_entry_init(ca_config_entry *entry,
                                          const cx_allocator *alloc);

/** @brief Deep-copy an entry. */
CAUDIO_API ca_status ca_config_entry_copy(ca_config_entry *dst,
                                          const ca_config_entry *src,
                                          const cx_allocator *alloc);

/** @brief Release an entry's strings and zero it. */
CAUDIO_API void ca_config_entry_destroy(ca_config_entry *entry);

/**
 * @brief Destroy a vector returned by ca_config_list_raw().
 *
 * Destroys every entry's strings, then the vector itself and sets @p out to
 * NULL. Safe on NULL.
 */
CAUDIO_API void ca_config_entries_destroy(cx_vector **out);

/**
 * @brief Read a raw value by key.
 *
 * @return CA_OK, CA_NOT_FOUND when the file or key is absent, CA_INVALID_ARGUMENT
 *         when the root is not an object, CA_JSON_ERROR for malformed JSON, or
 *         an I/O status.
 */
CAUDIO_API ca_status ca_config_get_raw(const char *path, const char *key,
                                       cx_string **out,
                                       const cx_allocator *alloc,
                                       ca_error *err);

/**
 * @brief Set a raw value by key, writing the canonical pretty JSON file.
 *
 * Non-empty @p value is parsed as JSON when possible; otherwise it is stored as
 * a JSON string. An empty @p value is stored as an empty string. A missing or
 * corrupt file is replaced by an object containing just this key.
 */
CAUDIO_API ca_status ca_config_set_raw(const char *path, const char *key,
                                       const char *value,
                                       const cx_allocator *alloc,
                                       ca_error *err);

/**
 * @brief List all raw pairs in document order, skipping empty and "type" keys.
 *
 * A missing or empty file yields an empty vector. An unreadable or malformed
 * file is an error.
 */
CAUDIO_API ca_status ca_config_list_raw(const char *path, cx_vector **out,
                                        const cx_allocator *alloc,
                                        ca_error *err);

/**
 * @brief Delete a raw key.
 *
 * @return CA_OK, CA_INVALID_ARGUMENT for an empty key or non-object root,
 *         CA_NOT_FOUND when the file or key is absent, or an I/O status.
 */
CAUDIO_API ca_status ca_config_delete_raw(const char *path, const char *key,
                                          const cx_allocator *alloc,
                                          ca_error *err);

/**
 * @brief Remove the configuration file entirely.
 *
 * A missing file is a successful no-op.
 */
CAUDIO_API ca_status ca_config_reset_all_raw(const char *path, ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_CONFIG_CONFIG_H */
