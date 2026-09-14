module;
#include <sqlite3.h>

#include <expected>
#include <string>
#include <string_view>
#include <utility>

/**
 * @file transaction.cppm
 * @brief RAII BEGIN IMMEDIATE / COMMIT / ROLLBACK helper.
 * @ingroup caudio_db
 * @details DbTransaction::begin() executes BEGIN IMMEDIATE; commit() or
 * rollback() finalizes. Destructor rolls back if not committed. Lock
 * ordering: caller must hold Database::mutex() before begin().
 */

module caudio.db:DbTransaction;

import caudio.utils;
import :detail;

namespace caudio::db {

/**
 * @brief RAII transaction guard.
 * @ingroup caudio_db
 * @details Begin with DbTransaction::begin(); commit() on success.
 * If neither commit() nor rollback() is called, destructor rolls back.
 * Not thread-safe; caller must hold the DB mutex.
 */
class DbTransaction final {
  public:
    /**
     * @brief Begins a transaction with BEGIN IMMEDIATE.
     * @ingroup caudio_db
     * @param db SQLite handle (must not be null).
     * @return Transaction guard or Error with StatusCode::InvalidArg if db is null,
     * StatusCode::Internal if BEGIN fails.
     * @par Thread safety
     * Caller must hold Database::mutex().
     */
    static std::expected<DbTransaction, caudio::utils::Error> begin(sqlite3* db) {
        if (!db) {
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "null db")};
        }
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "BEGIN failed";
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, msg)};
        }
        return DbTransaction{db, false};
    }

    /**
     * @brief Rolls back if not yet committed.
     * @ingroup caudio_db
     */
    ~DbTransaction() {
        if (db_ && !committed_) {
            char* err = nullptr;
            internal::SqliteErrGuard guard{err};
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
        }
    }
    DbTransaction(const DbTransaction&) = delete;
    DbTransaction& operator=(const DbTransaction&) = delete;
    /**
     * @brief Move-constructs, transferring ownership.
     * @ingroup caudio_db
     * @param o Source.
     */
    DbTransaction(DbTransaction&& o) noexcept
        : db_(std::exchange(o.db_, nullptr)), committed_(std::exchange(o.committed_, true)) {}
    /**
     * @brief Move-assigns, rolling back any active transaction first.
     * @ingroup caudio_db
     * @param o Source.
     * @return *this
     */
    DbTransaction& operator=(DbTransaction&& o) noexcept {
        if (this != &o) {
            if (db_ && !committed_) {
                char* err = nullptr;
                internal::SqliteErrGuard guard{err};
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
            }
            db_ = std::exchange(o.db_, nullptr);
            committed_ = std::exchange(o.committed_, true);
        }
        return *this;
    }
    /**
     * @brief Commits the transaction.
     * @ingroup caudio_db
     * @return Success or Error with StatusCode::Internal if no active transaction or COMMIT fails.
     */
    [[nodiscard]] std::expected<void, caudio::utils::Error> commit() {
        if (!db_) {
            return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            "no active DbTransaction"));
        }
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "commit failed";
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, msg));
        }
        committed_ = true;
        db_ = nullptr;
        return {};
    }
    /**
     * @brief Rolls back the transaction.
     * @ingroup caudio_db
     * @return Success or Error with StatusCode::Internal if no active transaction or ROLLBACK fails.
     */
    [[nodiscard]] std::expected<void, caudio::utils::Error> rollback() {
        if (!db_) {
            return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            "no active DbTransaction"));
        }
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "rollback failed";
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, msg));
        }
        committed_ = true;
        db_ = nullptr;
        return {};
    }

  private:
    explicit DbTransaction(sqlite3* db, bool committed) : db_(db), committed_(committed) {}

    sqlite3* db_{nullptr};  ///< Borrowed handle.
    bool committed_{false}; ///< True after commit/rollback or move.
};

} // namespace caudio::db
