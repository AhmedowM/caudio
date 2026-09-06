module;
#include <sqlite3.h>

#include <expected>
#include <string>
#include <string_view>

module caudio.db:transaction;

import caudio.utils;

namespace caudio::db {

class Transaction final {
public:
    explicit Transaction(sqlite3* db) : db_(db) {
        if (db_) {
            char* err = nullptr;
            int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
            if (rc != SQLITE_OK) {
                std::string msg = err ? err : "BEGIN failed";
                if (err) sqlite3_free(err);
                // We don't throw, but mark as failed
                db_ = nullptr;
            }
        }
    }
    ~Transaction() {
        if (db_ && !committed_) {
            char* err = nullptr;
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
            if (err) sqlite3_free(err);
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&& o) noexcept : db_(o.db_), committed_(o.committed_) {
        o.db_ = nullptr;
        o.committed_ = true;
    }
    Transaction& operator=(Transaction&& o) noexcept {
        if (this != &o) {
            if (db_ && !committed_) {
                char* err = nullptr;
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
                if (err) sqlite3_free(err);
            }
            db_ = o.db_;
            committed_ = o.committed_;
            o.db_ = nullptr;
            o.committed_ = true;
        }
        return *this;
    }
    [[nodiscard]] std::expected<void, caudio::utils::Error> commit() {
        if (!db_) {
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Internal, "no active transaction"));
        }
        char* err = nullptr;
        int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "commit failed";
            if (err) sqlite3_free(err);
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Internal, msg));
        }
        committed_ = true;
        db_ = nullptr;
        return {};
    }
    [[nodiscard]] std::expected<void, caudio::utils::Error> rollback() {
        if (!db_) {
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Internal, "no active transaction"));
        }
        char* err = nullptr;
        int rc = sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "rollback failed";
            if (err) sqlite3_free(err);
            return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Internal, msg));
        }
        committed_ = true;
        db_ = nullptr;
        return {};
    }

private:
    sqlite3* db_{nullptr};
    bool committed_{false};
};

} // namespace caudio::db