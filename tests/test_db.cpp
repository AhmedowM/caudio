#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

TEST_CASE("SQLite open in-memory", "[db_sqlite]") {
    sqlite3* db{nullptr};
    int rc = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    REQUIRE(rc == SQLITE_OK);
    if (db)
        sqlite3_close(db);
}

TEST_CASE("SQLite create table and insert", "[db_sqlite]") {
    sqlite3* db{nullptr};
    int rc = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    REQUIRE(rc == SQLITE_OK);
    if (db) {
        const char* sql = "CREATE TABLE tracks (id INTEGER PRIMARY KEY, name TEXT, path TEXT, "
                          "fingerprint BLOB, library_id INTEGER)";
        rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        REQUIRE(rc == SQLITE_OK);

        sql = "INSERT INTO tracks (name, path, fingerprint, library_id) VALUES (?, ?, ?, ?)";
        sqlite3_stmt* stmt{nullptr};
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        REQUIRE(rc == SQLITE_OK);

        sqlite3_bind_text(stmt, 1, "Test Track", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, "/path/to/track.mp3", -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 3, "\x00\x01\x02\x03", 4, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, 1);

        rc = sqlite3_step(stmt);
        REQUIRE(rc == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

TEST_CASE("SQLite list rows", "[db_sqlite]") {
    sqlite3* db{nullptr};
    int rc = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    REQUIRE(rc == SQLITE_OK);
    if (db) {
        sqlite3_exec(db, "CREATE TABLE tracks (id INTEGER PRIMARY KEY, name TEXT)", nullptr,
                     nullptr, nullptr);

        sqlite3_stmt* stmt{nullptr};
        const char* sql = "INSERT INTO tracks (name) VALUES ('Track 1')";
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        REQUIRE(rc == SQLITE_OK);
        rc = sqlite3_step(stmt);
        REQUIRE(rc == SQLITE_DONE);
        sqlite3_finalize(stmt);

        sql = "SELECT name FROM tracks";
        rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        REQUIRE(rc == SQLITE_OK);
        rc = sqlite3_step(stmt);
        REQUIRE(rc == SQLITE_ROW);
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        REQUIRE(name != nullptr);
        REQUIRE(std::string(name) == "Track 1");
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}