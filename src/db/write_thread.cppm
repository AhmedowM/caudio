module;
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

/**
 * @file write_thread.cppm
 * @brief Background write batching for SQLite.
 * @ingroup caudio_db
 * @details Single-consumer background thread that serializes `WriteOp`s
 * queued via an `MpscQueue<WriteOp>`. Batch size is set at construction
 * (`writeBatchSize`, default 256) and forwarded to the queue capacity.
 * The queue is bounded: `push()` returns `Busy` when full.
 */

export module caudio.db:write_thread;

import caudio.utils;
import :SqliteStatement;
import :detail;

export namespace caudio::db {

/**
 * @brief Single write operation submitted to `WriterThread`.
 * @ingroup caudio_db
 * @details Either `stmt` or `sql` is executed; `cb` is invoked outside
 * the worker lock after execution with success or `Corrupt` on failure.
 */
struct WriteOp {
    std::string sql;                                                                    ///< Raw SQL (used when `stmt` is null).
    std::unique_ptr<SqliteStatement> stmt;                                              ///< Prepared statement to step.
    std::move_only_function<void(std::expected<void, caudio::utils::Error>)> cb;         ///< Completion callback.

    WriteOp() = default;
    /**
     * @brief Constructs a write operation.
     * @ingroup caudio_db
     * @param s SQL text.
     * @param st Prepared statement (may be null if using `s`).
     * @param c Completion callback (may be empty).
     */
    WriteOp(std::string s, std::unique_ptr<SqliteStatement> st,
            std::move_only_function<void(std::expected<void, caudio::utils::Error>)> c)
        : sql(std::move(s)), stmt(std::move(st)), cb(std::move(c)) {}
    WriteOp(const WriteOp&) = delete;
    WriteOp& operator=(const WriteOp&) = delete;
    WriteOp(WriteOp&&) noexcept = default;
    WriteOp& operator=(WriteOp&&) noexcept = default;
    ~WriteOp() = default;
};

/**
 * @brief Background writer that serializes SQLite writes off the hot path.
 * @ingroup caudio_db
 * @details Thread-safety and batching:
 * - `queue_` is a bounded `MpscQueue<WriteOp>` with capacity `writeBatchSize`
 *   (default 256, clamped to >=1). `push()` fails with `Busy` when full.
 * - `db_` is a raw `sqlite3*` borrowed from `Database`; set in `open()` and
 *   cleared on move/close. No ownership.
 * - `thread_` is a `std::jthread` running `worker()` with a `stop_token`.
 * - `mtx_` + `cv_` coordinate `flush()` and the worker's wait. `in_flight_`
 *   counts ops currently being executed (outside the lock).
 * - Worker pops one op at a time (not batched under a single transaction in
 *   the current implementation; batching is at the queue-capacity level).
 * - `close()` requests stop, notifies, joins, then drains remaining ops.
 * - Move operations transfer queue/db/thread/in_flight; source is left empty.
 */
class WriterThread final {
  public:
    /**
     * @brief Constructs the writer with given batch/queue capacity.
     * @ingroup caudio_db
     * @param writeBatchSize Queue capacity; 0 is normalized to 256.
     */
    explicit WriterThread(std::size_t writeBatchSize = 256)
        : queue_(std::make_unique<caudio::utils::MpscQueue<WriteOp>>(
              writeBatchSize > 0 ? writeBatchSize : 256)) {}
    /** @brief Closes the thread and drains the queue. @ingroup caudio_db */
    ~WriterThread() {
        close();
    }

    WriterThread(const WriterThread&) = delete;
    WriterThread& operator=(const WriterThread&) = delete;

    /**
     * @brief Move-constructs, transferring queue/db/thread/in_flight.
     * @ingroup caudio_db
     * @param o Source; left with null db and empty thread.
     */
    WriterThread(WriterThread&& o) noexcept
        : queue_(std::move(o.queue_)), db_(std::exchange(o.db_, nullptr)),
          thread_(std::move(o.thread_)), in_flight_(o.in_flight_.load(std::memory_order_acquire)) {}
    /**
     * @brief Move-assigns; closes current thread first.
     * @ingroup caudio_db
     * @param o Source.
     * @return *this
     */
    WriterThread& operator=(WriterThread&& o) noexcept {
        if (this != &o) {
            close();
            queue_ = std::move(o.queue_);
            db_ = std::exchange(o.db_, nullptr);
            thread_ = std::move(o.thread_);
            in_flight_.store(o.in_flight_.load(std::memory_order_acquire),
                             std::memory_order_release);
        }
        return *this;
    }

    /**
     * @brief Starts the background thread for the given handle.
     * @ingroup caudio_db
     * @param db Borrowed sqlite3 handle (must outlive the thread).
     * @details No-op if already running (`thread_.joinable()`).
     * @par Thread safety
     * Not thread-safe with concurrent `open`/`close`; caller must serialize.
     */
    void open(sqlite3* db) {
        if (thread_.joinable())
            return;
        db_ = db;
        thread_ = std::jthread{[this](std::stop_token st) { worker(st); }};
    }

    /**
     * @brief Stops the thread and drains remaining ops.
     * @ingroup caudio_db
     * @details Requests stop, notifies `cv_`, joins. Remaining queued ops
     * are popped and destroyed (their statements finalized).
     */
    void close() {
        if (thread_.joinable()) {
            thread_.request_stop();
            cv_.notify_all();
            thread_.join();
        }
        // drain remaining stmts - WriteOp destructor handles finalization
        while (true) {
            auto v = queue_->pop();
            if (!v)
                break;
        }
    }

    /**
     * @brief Enqueues a write operation.
     * @ingroup caudio_db
     * @param sql SQL text (used if `stmt` is null).
     * @param stmt Prepared statement to execute (may be null).
     * @param cb Completion callback invoked after execution.
     * @return Success or `Error` with `StatusCode::Busy` if queue is full.
     * @par Thread safety
     * Thread-safe (MPSC); multiple producers may call concurrently.
     */
    std::expected<void, caudio::utils::Error>
    push(std::string sql, std::unique_ptr<SqliteStatement> stmt,
         std::move_only_function<void(std::expected<void, caudio::utils::Error>)> cb) {
        WriteOp op{std::move(sql), std::move(stmt), std::move(cb)};
        auto r = queue_->push(std::move(op));
        if (!r)
            return std::unexpected{r.error()};
        cv_.notify_one();
        return {};
    }

    /**
     * @brief Waits until the queue is empty and no op is in flight.
     * @ingroup caudio_db
     * @return Success, or `Error` with `StatusCode::Busy` on 200 ms timeout.
     * @details Waits on `cv_` with predicate `queue_->empty() && in_flight_ == 0`.
     * Re-checks predicate after timeout to avoid spurious failure.
     * @par Thread safety
     * Thread-safe; blocks caller.
     */
    std::expected<void, caudio::utils::Error> flush() {
        std::unique_lock<std::mutex> lk(mtx_);
        bool done = cv_.wait_for(lk, std::chrono::milliseconds{200}, [this] {
            return queue_->empty() && in_flight_.load(std::memory_order_acquire) == 0;
        });
        if (done)
            return {};
        // predicate false after timeout — re-check without race
        if (queue_->empty() && in_flight_.load(std::memory_order_acquire) == 0)
            return {};
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Busy, "flush timeout")};
    }

    /**
     * @brief Checks if the queue is empty.
     * @ingroup caudio_db
     * @return true if no pending ops.
     */
    bool empty() const {
        return queue_->empty();
    }
    /**
     * @brief Returns number of pending ops.
     * @ingroup caudio_db
     * @return Queue size.
     */
    std::size_t size() const {
        return queue_->size();
    }

  private:
    /**
     * @brief Worker loop: waits for work, executes one op at a time.
     * @ingroup caudio_db
     * @param st Stop token for cooperative cancellation.
     * @details Pops outside the lock, increments `in_flight_`, executes
     * `stmt->stepDone()` or `sqlite3_exec(sql)`, maps non-DONE/ROW/OK to
     * `StatusCode::Corrupt`, decrements `in_flight_`, notifies `cv_`, then
     * invokes the callback outside the lock. Sleeps 200 ms between polls.
     */
    void worker(std::stop_token st) {
        while (!st.stop_requested()) {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds{200},
                         [this, &st] { return !queue_->empty() || st.stop_requested(); });
            if (st.stop_requested() && queue_->empty())
                break;
            // pop all available up to batch? single for now; don't hold lock during exec
            lk.unlock();
            auto popped = queue_->pop();
            if (!popped.has_value()) {
                // if empty, continue waiting
                if (st.stop_requested())
                    break;
                continue;
            }
            WriteOp op = std::move(popped.value());
            in_flight_.fetch_add(1, std::memory_order_acq_rel);
            caudio::utils::Error err{};
            bool ok = true;
            if (op.stmt) {
                int rc = op.stmt->stepDone();
                if (rc != SQLITE_DONE && rc != SQLITE_ROW && rc != SQLITE_OK) {
                    err = caudio::utils::makeError(caudio::utils::StatusCode::Corrupt,
                                                   sqlite3_errmsg(db_));
                    ok = false;
                }
                // stmt finalized by unique_ptr destructor
            } else if (!op.sql.empty() && db_) {
                char* e = nullptr;
                internal::SqliteErrGuard guard{e};
                int rc = sqlite3_exec(db_, op.sql.c_str(), nullptr, nullptr, &e);
                if (rc != SQLITE_OK) {
                    err = caudio::utils::makeError(caudio::utils::StatusCode::Corrupt,
                                                   e ? e : sqlite3_errmsg(db_));
                    ok = false;
                }
            }
            in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            {
                std::lock_guard<std::mutex> lk(mtx_);
                cv_.notify_all();
            }
            // callback outside lock (notify already sent)
            if (op.cb) {
                if (ok)
                    op.cb(std::expected<void, caudio::utils::Error>{});
                else
                    op.cb(std::unexpected{err});
            }
        }
    }

    std::unique_ptr<caudio::utils::MpscQueue<WriteOp>> queue_;   ///< Bounded queue (capacity = batch size).
    sqlite3* db_{nullptr};                                        ///< Borrowed handle (not owned).
    std::jthread thread_{};                                       ///< Worker thread.
    std::mutex mtx_;                                              ///< Protects cv_/flush coordination.
    std::condition_variable cv_;                                  ///< Notified on push and op completion.
    std::atomic<int> in_flight_{0};                               ///< Ops currently executing.
};

} // namespace caudio::db
