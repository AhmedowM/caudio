module;
#include <sqlite3.h>

#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

module caudio.db:statement;

import caudio.utils;

namespace caudio::db {

class Statement final {
  public:
    Statement() = default;
    ~Statement() {
        if (stmt_)
            sqlite3_finalize(stmt_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& o) noexcept : stmt_(o.stmt_) {
        o.stmt_ = nullptr;
    }
    Statement& operator=(Statement&& o) noexcept {
        if (this != &o) {
            if (stmt_)
                sqlite3_finalize(stmt_);
            stmt_ = o.stmt_;
            o.stmt_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] std::expected<void, caudio::utils::Error> prepare(sqlite3* db,
                                                                    std::string_view sql) {
        if (stmt_)
            sqlite3_finalize(stmt_);
        stmt_ = nullptr;
        int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            std::string msg = db ? sqlite3_errmsg(db) : "prepare failed";
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, msg)};
        }
        return {};
    }
    void bindInt(int idx, int64_t v) {
        if (stmt_)
            sqlite3_bind_int64(stmt_, idx, v);
    }
    void bindDouble(int idx, double v) {
        if (stmt_)
            sqlite3_bind_double(stmt_, idx, v);
    }
    void bindText(int idx, std::string_view v) {
        if (!stmt_)
            return;
        if (v.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return;
        sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }
    void bindBlob(int idx, const void* data, int n) {
        if (stmt_)
            sqlite3_bind_blob(stmt_, idx, data, n, SQLITE_TRANSIENT);
    }
    void bindNull(int idx) {
        if (stmt_)
            sqlite3_bind_null(stmt_, idx);
    }
    [[nodiscard]] bool step() {
        if (!stmt_)
            return false;
        int rc = sqlite3_step(stmt_);
        return rc == SQLITE_ROW;
    }
    [[nodiscard]] int stepDone() {
        if (!stmt_)
            return SQLITE_ERROR;
        return sqlite3_step(stmt_);
    }
    int64_t columnInt(int idx) const {
        return stmt_ ? sqlite3_column_int64(stmt_, idx) : 0;
    }
    double columnDouble(int idx) const {
        return stmt_ ? sqlite3_column_double(stmt_, idx) : 0.0;
    }
    std::string columnText(int idx) const {
        if (!stmt_)
            return {};
        const unsigned char* txt = sqlite3_column_text(stmt_, idx);
        return txt ? std::string(reinterpret_cast<const char*>(txt)) : std::string{};
    }
    void reset() {
        if (stmt_) {
            sqlite3_reset(stmt_);
            sqlite3_clear_bindings(stmt_);
        }
    }
    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return stmt_;
    }

  private:
    sqlite3_stmt* stmt_{nullptr};
};

} // namespace caudio::db