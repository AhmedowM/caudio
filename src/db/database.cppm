module;
#include <string>
#include <string_view>
#include <memory>
#include <expected>
#include <shared_mutex>
#include <sqlite3.h>
#include <filesystem>
#include <limits>

export module caudio.db;

import caudio.utils;

export namespace caudio::db {

// Use caudio::utils::Error directly; our Error struct uses caudio::utils::Result
// for the code field, matching the Expected<T, Error> pattern.

class Statement final {
public:
  Statement() = default;
  ~Statement() { if (stmt_) sqlite3_finalize(stmt_); }

  // Non-copyable
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  // Movable
  Statement(Statement&& o) noexcept : stmt_(o.stmt_) { o.stmt_ = nullptr; }
  Statement& operator=(Statement&& o) noexcept {
    if (this != &o) {
      if (stmt_) sqlite3_finalize(stmt_);
      stmt_ = o.stmt_;
      o.stmt_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] std::expected<void, caudio::utils::Error> prepare(sqlite3* db, std::string_view sql) {
    if (stmt_) sqlite3_finalize(stmt_);
    int rc = sqlite3_prepare_v2(db, sql.data(), sql.size(), &stmt_, nullptr);
    if (rc != SQLITE_OK) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "sqlite3_prepare_v2 failed")};
    return {};
  }

  void bindInt(int idx, int64_t val) {
    if (!stmt_) return;
    sqlite3_bind_int64(stmt_, idx, val);
  }

  void bindText(int idx, std::string_view val) {
    if (!stmt_) return;
    if (val.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return;
    sqlite3_bind_text(stmt_, idx, val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
  }

  [[nodiscard]] bool step() {
    if (!stmt_) return false;
    int rc = sqlite3_step(stmt_);
    return rc == SQLITE_ROW;
  }

  int64_t columnInt(int idx) const {
    if (!stmt_) return 0;
    return sqlite3_column_int64(stmt_, idx);
  }

  std::string columnText(int idx) const {
    if (!stmt_) return {};
    const char* v = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, idx));
    return v ? std::string(v) : std::string{};
  }

  void reset() {
    if (stmt_) sqlite3_reset(stmt_);
  }

  [[nodiscard]] explicit operator bool() const { return stmt_ != nullptr; }

 private:
   sqlite3_stmt* stmt_{nullptr};
};

// ---------------------------------------------------------------------------
// Transaction RAII class — BEGIN IMMEDIATE / COMMIT / ROLLBACK
// ---------------------------------------------------------------------------
class Transaction final {
 public:
  explicit Transaction(sqlite3* db) : db_(db) {
    if (db_) {
      sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    }
  }

  ~Transaction() {
    if (db_) {
      sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
  }

  // Commit the transaction; returns Error on failure
  [[nodiscard]] caudio::utils::Error commit() {
    if (!db_) return caudio::utils::makeError(caudio::utils::Result::Internal, "no database");
    char* errMsg{nullptr};
    int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
      std::string msg = errMsg ? errMsg : "unknown error";
      sqlite3_free(errMsg);
      return caudio::utils::makeError(caudio::utils::Result::Internal, msg);
    }
    return caudio::utils::Error{caudio::utils::Result::Ok};
  }

  // Non-copyable
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  // Movable
  Transaction(Transaction&& o) noexcept : db_(o.db_) { o.db_ = nullptr; }
  Transaction& operator=(Transaction&& o) noexcept {
    if (this != &o) {
      // Rollback old, commit new
      if (db_) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
      }
      db_ = o.db_;
      o.db_ = nullptr;
    }
    return *this;
  }

 private:
  sqlite3* db_{nullptr};
};

} // namespace caudio::db

// ---------------------------------------------------------------------------
// Database core with std::shared_mutex + Transaction + Statement cache
// ---------------------------------------------------------------------------
class Database final {
public:
  Database() = default;
  ~Database() { if (db_) sqlite3_close(db_); }

  // Non-copyable
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // Movable
  Database(Database&& o) noexcept : db_(o.db_) { o.db_ = nullptr; }
  Database& operator=(Database&& o) noexcept {
    if (this != &o) {
      if (db_) sqlite3_close(db_);
      db_ = o.db_;
    }
    return *this;
  }

  // Open/create SQLite database at path
  // Uses :memory: if path ends with ":memory:" or path is empty
  static std::expected<std::unique_ptr<Database>, caudio::utils::Error> open(std::string_view path) {
    std::string dbPath = path.empty() ? ":memory:" : std::string(path);
    sqlite3* raw{nullptr};
    int rc = sqlite3_open_v2(dbPath.c_str(), &raw,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI,
                             nullptr);
    if (rc != SQLITE_OK) {
      return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot open database: " + std::string(sqlite3_errmsg(raw)))};
    }
    // Enable WAL mode (as in the C port)
    rc = sqlite3_exec(raw, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
      sqlite3_close(raw);
      return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot set WAL mode")};
    }
    auto db = std::make_unique<Database>();
    db->db_ = raw;
    return std::move(db);
  }

  // Get a single track by primary key id
  std::expected<std::string, caudio::utils::Error> getTrackName(int64_t id) {
    std::shared_lock lock{m_};
    if (!db_) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no database")};
    db::Statement stmt;
    if (auto err = stmt.prepare(db_, "SELECT name FROM tracks WHERE id = ?"); !err) return std::unexpected{err};
    stmt.bindInt(1, id);
    if (!stmt.step()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
    return stmt.columnText(0);
  }

  // List all tracks, optionally filtered by library_id
  std::expected<std::vector<std::tuple<int64_t, std::string>>, caudio::utils::Error> listTracks(int64_t libraryId = 0) {
    std::shared_lock lock{m_};
    if (!db_) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no database")};
    db::Statement stmt;
    if (auto err = stmt.prepare(db_, "SELECT id, name FROM tracks"); !err) return std::unexpected{err};
    if (libraryId > 0) {
      stmt.bindInt(1, libraryId);
    }
    std::vector<std::tuple<int64_t, std::string>> result;
    while (stmt.step()) {
      result.emplace_back(stmt.columnInt(0), stmt.columnText(1));
    }
    return result;
  }

  // Insert a track via a Transaction
  std::expected<void, caudio::utils::Error> insertTrack(int64_t libraryId, std::string_view name, std::string_view path,
                                                         std::string_view fingerprint = {}) {
    std::unique_lock lock{m_};
    if (!db_) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no database")};
    Transaction tx{db_};
    db::Statement stmt;
    if (auto err = stmt.prepare(db_,
          "INSERT INTO tracks (library_id, path, fingerprint, name, created_at) "
          "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)"); !err) {
      return std::unexpected{err};
    }
    stmt.bindInt(1, libraryId);
    stmt.bindText(2, path);
    if (!fingerprint.empty()) {
      stmt.bindText(3, fingerprint);
    } else {
      stmt.bindInt(3, 0); // NULL or 0 sentinel
    }
    stmt.bindText(4, name);

    if (!stmt.step()) {
      return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "insert step failed")};
    }
    auto err = tx.commit();
    if (err.code != caudio::utils::Result::Ok) return std::unexpected{err};
    return {};
  }

private:
  sqlite3* db_{nullptr};
  mutable std::shared_mutex m_;

  friend std::expected<void, caudio::utils::Error> insertTrack(Database& db, int64_t libraryId, std::string_view name,
                                                               std::string_view path, std::string_view fingerprint);
};

// Free function: insert helper using Transaction
std::expected<void, caudio::utils::Error> insertTrack(Database& db, int64_t libraryId, std::string_view name,
                                                     std::string_view path, std::string_view fingerprint) {
  std::unique_lock lock{db.m_};
  if (!db.db_) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no database")};
  Transaction tx{db.db_};
  db::Statement stmt;
  if (auto err = stmt.prepare(db.db_,
        "INSERT INTO tracks (library_id, path, fingerprint, name, created_at) "
        "VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)"); !err) {
    return std::unexpected{err};
  }
  stmt.bindInt(1, libraryId);
  stmt.bindText(2, path);
  if (!fingerprint.empty()) {
    stmt.bindText(3, fingerprint);
  } else {
    stmt.bindInt(3, 0);
  }
  stmt.bindText(4, name);

  if (!stmt.step()) {
    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "insert step failed")};
  }
  auto err = tx.commit();
  if (err.code != caudio::utils::Result::Ok) return std::unexpected{err};
  return {};
}

} // namespace caudio::db