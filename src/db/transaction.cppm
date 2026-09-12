module;
#include <sqlite3.h>

#include <expected>
#include <string>
#include <string_view>
#include <utility>

module caudio.db:DbTransaction;

import caudio.utils;
import :detail;

namespace caudio::db {

class DbTransaction final {
  public:
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

    ~DbTransaction() {
        if (db_ && !committed_) {
            char* err = nullptr;
            internal::SqliteErrGuard guard{err};
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
        }
    }
    DbTransaction(const DbTransaction&) = delete;
    DbTransaction& operator=(const DbTransaction&) = delete;
    DbTransaction(DbTransaction&& o) noexcept
        : db_(std::exchange(o.db_, nullptr)), committed_(std::exchange(o.committed_, true)) {}
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

    sqlite3* db_{nullptr};
    bool committed_{false};
};

} // namespace caudio::db
