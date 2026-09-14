module;
#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

/**
 * @file statement.cppm
 * @brief RAII wrapper for sqlite3_stmt.
 * @ingroup caudio_db
 * @details Non-copyable, movable. Owns a sqlite3_stmt* and finalizes on
 * destruction/move. Provides prepare/bind/step/column/reset helpers.
 * All operations are thin wrappers over the SQLite C API; no locking is
 * performed here — callers must hold the appropriate Database mutex.
 */

module caudio.db:SqliteStatement;

import caudio.utils;

namespace caudio::db {

/**
 * @brief RAII prepared statement.
 * @ingroup caudio_db
 * @details Wraps sqlite3_stmt*; finalized on destruction. Move transfers
 * ownership. All bind/step methods are no-ops if no statement is prepared.
 * Thread-safety: not thread-safe; external synchronization required.
 */
class SqliteStatement final {
  public:
    /** @brief Default-constructs with no statement. @ingroup caudio_db */
    SqliteStatement() = default;
    /** @brief Finalizes the statement if active. @ingroup caudio_db */
    ~SqliteStatement() {
        if (stmt_)
            sqlite3_finalize(stmt_);
    }
    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;
    /**
     * @brief Move-constructs, transferring ownership.
     * @ingroup caudio_db
     * @param o Source; left with nullptr.
     */
    SqliteStatement(SqliteStatement&& o) noexcept : stmt_(o.stmt_) {
        o.stmt_ = nullptr;
    }
    /**
     * @brief Move-assigns, finalizing any existing statement.
     * @ingroup caudio_db
     * @param o Source.
     * @return *this
     */
    SqliteStatement& operator=(SqliteStatement&& o) noexcept {
        if (this != &o) {
            if (stmt_)
                sqlite3_finalize(stmt_);
            stmt_ = o.stmt_;
            o.stmt_ = nullptr;
        }
        return *this;
    }
    /**
     * @brief Prepares a statement.
     * @ingroup caudio_db
     * @param db SQLite handle.
     * @param sql SQL text.
     * @return Success or Error with StatusCode::Internal and sqlite error message.
     */
    [[nodiscard]] std::expected<void, caudio::utils::Error> prepare(sqlite3* db,
                                                                    std::string_view sql) {
        if (stmt_)
            sqlite3_finalize(stmt_);
        stmt_ = nullptr;
        int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            std::string msg = db ? sqlite3_errmsg(db) : "prepare failed";
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, msg)};
        }
        return {};
    }
    /**
     * @brief Binds an int64 at 1-based index.
     * @ingroup caudio_db
     * @param idx Parameter index (1-based).
     * @param v Value.
     */
    void bindInt(int idx, int64_t v) {
        if (stmt_)
            sqlite3_bind_int64(stmt_, idx, v);
    }
    /**
     * @brief Binds a double at 1-based index.
     * @ingroup caudio_db
     * @param idx Parameter index.
     * @param v Value.
     */
    void bindDouble(int idx, double v) {
        if (stmt_)
            sqlite3_bind_double(stmt_, idx, v);
    }
    /**
     * @brief Binds text at 1-based index (SQLITE_TRANSIENT).
     * @ingroup caudio_db
     * @param idx Parameter index.
     * @param v Text view to copy.
     */
    void bindText(int idx, std::string_view v) {
        if (!stmt_)
            return;
        if (v.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return;
        sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }
    /**
     * @brief Binds a blob at 1-based index (SQLITE_TRANSIENT).
     * @ingroup caudio_db
     * @param idx Parameter index.
     * @param data Bytes to copy.
     */
    void bindBlob(int idx, std::span<const std::byte> data) {
        if (stmt_)
            sqlite3_bind_blob(stmt_, idx, data.data(), static_cast<int>(data.size()),
                              SQLITE_TRANSIENT);
    }
    /** @brief Deprecated C-pointer overload — use span overload. @ingroup caudio_db */
    [[deprecated("use span overload")]] void bindBlob(int idx, const void* data, int n) {
        bindBlob(idx, std::span<const std::byte>{reinterpret_cast<const std::byte*>(data),
                                                 data && n > 0 ? static_cast<std::size_t>(n) : 0});
    }
    /**
     * @brief Binds NULL at 1-based index.
     * @ingroup caudio_db
     * @param idx Parameter index.
     */
    void bindNull(int idx) {
        if (stmt_)
            sqlite3_bind_null(stmt_, idx);
    }
    /**
     * @brief Steps to the next row.
     * @ingroup caudio_db
     * @return true if a row is available (SQLITE_ROW), false otherwise.
     */
    [[nodiscard]] bool step() {
        if (!stmt_)
            return false;
        int rc = sqlite3_step(stmt_);
        return rc == SQLITE_ROW;
    }
    /**
     * @brief Steps expecting completion (SQLITE_DONE).
     * @ingroup caudio_db
     * @return SQLite result code (SQLITE_DONE on success, SQLITE_ERROR if no statement).
     */
    [[nodiscard]] int stepDone() {
        if (!stmt_)
            return SQLITE_ERROR;
        return sqlite3_step(stmt_);
    }
    /**
     * @brief Reads an int64 column.
     * @ingroup caudio_db
     * @param idx Column index (0-based).
     * @return Column value or 0 if no statement.
     */
    int64_t columnInt(int idx) const {
        return stmt_ ? sqlite3_column_int64(stmt_, idx) : 0;
    }
    /**
     * @brief Reads a double column.
     * @ingroup caudio_db
     * @param idx Column index.
     * @return Column value or 0.0 if no statement.
     */
    double columnDouble(int idx) const {
        return stmt_ ? sqlite3_column_double(stmt_, idx) : 0.0;
    }
    /**
     * @brief Reads a text column as string.
     * @ingroup caudio_db
     * @param idx Column index.
     * @return Column text or empty if null/no statement.
     */
    std::string columnText(int idx) const {
        if (!stmt_)
            return {};
        const unsigned char* txt = sqlite3_column_text(stmt_, idx);
        return txt ? std::string(reinterpret_cast<const char*>(txt)) : std::string{};
    }
    /**
     * @brief Resets the statement and clears bindings for reuse.
     * @ingroup caudio_db
     */
    void reset() {
        if (stmt_) {
            sqlite3_reset(stmt_);
            sqlite3_clear_bindings(stmt_);
        }
    }
    /**
     * @brief Returns the raw sqlite3_stmt*.
     * @ingroup caudio_db
     * @return Borrowed pointer, may be null.
     */
    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return stmt_;
    }

  private:
    sqlite3_stmt* stmt_{nullptr}; ///< Owned handle, finalized on destruction.
};

} // namespace caudio::db
