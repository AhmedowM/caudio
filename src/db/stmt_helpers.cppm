module;
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file stmt_helpers.cppm
 * @brief Shared statement helpers for the db layer.
 * @ingroup caudio_db
 * @details Provides the canonical column list `kSelectTracksCols`,
 * the row-mapper `fillTrackFromStmt()` and the RAII `SqliteErrGuard`.
 * All helpers are header-only and live in `caudio::db::internal`.
 */

module caudio.db:stmt_helpers;

import caudio.utils;
import :types;

namespace caudio::db::internal {

/**
 * @brief Canonical SELECT column list for `tracks`.
 * @ingroup caudio_db
 * @details Order matches `fillTrackFromStmt()` indices 0..25. Keep in sync.
 */
inline constexpr std::string_view kSelectTracksCols =
    "SELECT id, fingerprint, path, deleted_at, size, mtime, duration, sample_rate, channels, "
    "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
    "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, library_id "
    "FROM tracks";

/**
 * @brief Reads a TEXT column as string_view (empty on NULL).
 * @ingroup caudio_db
 * @param stmt Statement handle.
 * @param col Zero-based column index.
 * @return View of the column text; empty if NULL.
 * @par Thread safety
 * Caller must hold appropriate DB lock; sqlite3 handles are not thread-safe.
 */
inline std::string_view columnText(sqlite3_stmt* stmt, int col) noexcept {
    auto* p = sqlite3_column_text(stmt, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

/**
 * @brief RAII guard that frees a `sqlite3_exec` error string.
 * @ingroup caudio_db
 * @details Wraps a `char*` reference returned via `sqlite3_exec(..., &err)`.
 * On destruction, calls `sqlite3_free` if non-null and nulls the reference.
 * Non-copyable.
 */
struct SqliteErrGuard {
    char*& ref;                                     ///< Reference to the error pointer.
    /**
     * @brief Constructs guard for the given error pointer.
     * @ingroup caudio_db
     * @param r Reference to `char*` that will receive the SQLite error.
     */
    explicit SqliteErrGuard(char*& r) : ref(r) {}
    SqliteErrGuard(const SqliteErrGuard&) = delete;
    SqliteErrGuard& operator=(const SqliteErrGuard&) = delete;
    /** @brief Frees the error string if set. @ingroup caudio_db */
    ~SqliteErrGuard() {
        if (ref) {
            sqlite3_free(ref);
            ref = nullptr;
        }
    }
};

/**
 * @brief Maps the current row of a `kSelectTracksCols` statement into a Track.
 * @ingroup caudio_db
 * @param stmt Prepared statement positioned on a row (via `step()`).
 * @param t Output track to populate.
 * @details Column order must match `kSelectTracksCols`:
 * 0:id 1:fingerprint(BLOB32) 2:path 3:deleted_at 4:size 5:mtime 6:duration
 * 7:sample_rate 8:channels 9:bitrate 10:title 11:artist 12:album
 * 13:album_artist 14:genre 15:year 16:track_num 17:disc_num 18:cover_art_path
 * 19:rating 20:play_count 21:last_played 22:date_added 23:last_scanned
 * 24:dirty 25:library_id. Fingerprint is zeroed if the blob is not 32 bytes.
 * @par Thread safety
 * Caller must hold the DB lock protecting the statement's connection.
 * @see kSelectTracksCols
 */
inline void fillTrackFromStmt(sqlite3_stmt* stmt, Track& t) {
    t.id = sqlite3_column_int64(stmt, 0);
    t.fingerprint.fill(0);
    if (sqlite3_column_bytes(stmt, 1) == 32) {
        std::memcpy(t.fingerprint.data(), sqlite3_column_blob(stmt, 1), 32);
    }
    t.path = columnText(stmt, 2);
    t.deleted_at = sqlite3_column_int64(stmt, 3);
    t.size = sqlite3_column_int64(stmt, 4);
    t.mtime = sqlite3_column_int64(stmt, 5);
    t.duration = sqlite3_column_double(stmt, 6);
    t.sample_rate = static_cast<uint32_t>(sqlite3_column_int(stmt, 7));
    t.channels = static_cast<uint32_t>(sqlite3_column_int(stmt, 8));
    t.bitrate = static_cast<int>(sqlite3_column_int(stmt, 9));
    t.title = columnText(stmt, 10);
    t.artist = columnText(stmt, 11);
    t.album = columnText(stmt, 12);
    t.album_artist = columnText(stmt, 13);
    t.genre = columnText(stmt, 14);
    t.year = static_cast<int>(sqlite3_column_int(stmt, 15));
    t.track_num = static_cast<int>(sqlite3_column_int(stmt, 16));
    t.disc_num = static_cast<int>(sqlite3_column_int(stmt, 17));
    t.cover_art_path = columnText(stmt, 18);
    t.rating = static_cast<int>(sqlite3_column_int(stmt, 19));
    t.play_count = sqlite3_column_int64(stmt, 20);
    t.last_played = sqlite3_column_int64(stmt, 21);
    t.date_added = sqlite3_column_int64(stmt, 22);
    t.last_scanned = sqlite3_column_int64(stmt, 23);
    t.dirty = sqlite3_column_int(stmt, 24) != 0;
    t.library_id = static_cast<int>(sqlite3_column_int(stmt, 25));
}

} // namespace caudio::db::internal
