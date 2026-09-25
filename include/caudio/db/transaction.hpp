#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <caudio/db/detail.hpp>
#include <caudio/utils.hpp>

/**
 * @file transaction.hpp
 * @brief RAII BEGIN IMMEDIATE / COMMIT / ROLLBACK helper.
 * @ingroup caudio_db
 * @details DbTransaction::begin() executes BEGIN IMMEDIATE; commit() or
 * rollback() finalizes. Destructor rolls back if not committed. Lock
 * ordering: caller must hold Database::mutex() before begin().
 */

// Forward declarations for SQLite handles (sqlite3.h stays in .cpp files).
struct sqlite3;
struct sqlite3_stmt;

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
    static std::expected<DbTransaction, caudio::utils::Error> begin(sqlite3* db);

    /**
     * @brief Rolls back if not yet committed.
     * @ingroup caudio_db
     */
    ~DbTransaction();
    DbTransaction(const DbTransaction&) = delete;
    DbTransaction& operator=(const DbTransaction&) = delete;
    /**
     * @brief Move-constructs, transferring ownership.
     * @ingroup caudio_db
     * @param o Source.
     */
    DbTransaction(DbTransaction&& o) noexcept;
    /**
     * @brief Move-assigns, rolling back any active transaction first.
     * @ingroup caudio_db
     * @param o Source.
     * @return *this
     */
    DbTransaction& operator=(DbTransaction&& o) noexcept;
    /**
     * @brief Commits the transaction.
     * @ingroup caudio_db
     * @return Success or Error with StatusCode::Internal if no active transaction or COMMIT fails.
     */
    [[nodiscard]] std::expected<void, caudio::utils::Error> commit();
    /**
     * @brief Rolls back the transaction.
     * @ingroup caudio_db
     * @return Success or Error with StatusCode::Internal if no active transaction or ROLLBACK
     * fails.
     */
    [[nodiscard]] std::expected<void, caudio::utils::Error> rollback();

  private:
    explicit DbTransaction(sqlite3* db, bool committed);

    sqlite3* db_{nullptr};  ///< Borrowed handle.
    bool committed_{false}; ///< True after commit/rollback or move.
};

} // namespace caudio::db
