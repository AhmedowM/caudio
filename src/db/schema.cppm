module;
#include <string_view>
#include <string>

export module caudio.db:schema;

export namespace caudio::db {

constexpr std::string_view kSchema =
    "CREATE TABLE IF NOT EXISTS tracks ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "library_id INTEGER, "
    "path TEXT NOT NULL, "
    "fingerprint BLOB, "
    "name TEXT NOT NULL, "
    "created_at TEXT DEFAULT CURRENT_TIMESTAMP"
    ");"
    "CREATE TABLE IF NOT EXISTS playlists ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "name TEXT NOT NULL, "
    "description TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS queue ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "track_id INTEGER, "
    "position INTEGER"
    ");";

} // namespace caudio::db
