module;
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

export module caudio.db:stmt_helpers;

import caudio.utils;
import :types;

export namespace caudio::db::internal {

inline constexpr std::string_view kSelectTracksCols =
    "SELECT id, fingerprint, path, deleted_at, size, mtime, duration, sample_rate, channels, "
    "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
    "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, library_id "
    "FROM tracks";

inline std::string_view columnText(sqlite3_stmt* stmt, int col) noexcept {
    auto* p = sqlite3_column_text(stmt, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

struct SqliteErrGuard {
    char*& ref;
    explicit SqliteErrGuard(char*& r) : ref(r) {}
    SqliteErrGuard(const SqliteErrGuard&) = delete;
    SqliteErrGuard& operator=(const SqliteErrGuard&) = delete;
    ~SqliteErrGuard() {
        if (ref) {
            sqlite3_free(ref);
            ref = nullptr;
        }
    }
};

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