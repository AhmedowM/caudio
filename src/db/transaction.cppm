module;
#include <sqlite3.h>

#include <expected>
#include <string>
#include <string_view>
#include <utility>

module caudio.db:transaction;

import caudio.utils;
import :detail;

namespace caudio::db {

class Transaction final {
  public:
    static std::expected<Transaction, caudio::utils::Error> begin(sqlite3* db) {
        if (!db) {
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "null db")};
        }
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "BEGIN failed";
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, msg)};
        }
        return Transaction{db, false};
    }

    ~Transaction() {
        if (db_ && !committed_) {
            char* err = nullptr;
            internal::SqliteErrGuard guard{err};
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&& o) noexcept
        : db_(std::exchange(o.db_, nullptr)), committed_(std::exchange(o.committed_, true)) {}
    Transaction& operator=(Transaction&& o) noexcept {
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
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::Internal, "no active transaction"));
        }
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "commit failed";
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Internal, msg));
        }
        committed_ = true;
        db_ = nullptr;
        return {};
    }
    [[nodiscard]] std::expected<void, caudio::utils::Error> rollback() {
        if (!db_) {
            return std::unexpected(
                caudio::utils::makeError(caudio::utils::Result::Internal, "no active transaction"));
        }
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "rollback failed";
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Internal, msg));
        }
        committed_ = true;
        db_ = nullptr;
        return {};
    }

  private:
    explicit Transaction(sqlite3* db, bool committed) : db_(db), committed_(committed) {}

    sqlite3* db_{nullptr};
    bool committed_{false};
};

} // namespace caudio::db
