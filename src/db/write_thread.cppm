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

export module caudio.db:write_thread;

import caudio.utils;
import :statement;
import :detail;

export namespace caudio::db {

struct WriteOp {
    std::string sql;
    std::unique_ptr<Statement> stmt;
    std::move_only_function<void(std::expected<void, caudio::utils::Error>)> cb;

    WriteOp() = default;
    WriteOp(std::string s, std::unique_ptr<Statement> st,
            std::move_only_function<void(std::expected<void, caudio::utils::Error>)> c)
        : sql(std::move(s)), stmt(std::move(st)), cb(std::move(c)) {}
    WriteOp(const WriteOp&) = delete;
    WriteOp& operator=(const WriteOp&) = delete;
    WriteOp(WriteOp&&) noexcept = default;
    WriteOp& operator=(WriteOp&&) noexcept = default;
    ~WriteOp() = default;
};

class WriterThread final {
  public:
    explicit WriterThread(std::size_t writeBatchSize = 256)
        : queue_(std::make_unique<caudio::utils::MpscQueue<WriteOp>>(
              writeBatchSize > 0 ? writeBatchSize : 256)) {}
    ~WriterThread() {
        close();
    }

    WriterThread(const WriterThread&) = delete;
    WriterThread& operator=(const WriterThread&) = delete;

    WriterThread(WriterThread&& o) noexcept
        : queue_(std::move(o.queue_)), db_(std::exchange(o.db_, nullptr)),
          thread_(std::move(o.thread_)), in_flight_(o.in_flight_.load(std::memory_order_acquire)) {}
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

    void open(sqlite3* db) {
        if (thread_.joinable())
            return;
        db_ = db;
        thread_ = std::jthread{[this](std::stop_token st) { worker(st); }};
    }

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

    std::expected<void, caudio::utils::Error>
    push(std::string sql, std::unique_ptr<Statement> stmt,
         std::move_only_function<void(std::expected<void, caudio::utils::Error>)> cb) {
        WriteOp op{std::move(sql), std::move(stmt), std::move(cb)};
        auto r = queue_->push(std::move(op));
        if (!r)
            return std::unexpected{r.error()};
        cv_.notify_one();
        return {};
    }

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
            caudio::utils::makeError(caudio::utils::Result::Busy, "flush timeout")};
    }

    bool empty() const {
        return queue_->empty();
    }
    std::size_t size() const {
        return queue_->size();
    }

  private:
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
                    err = caudio::utils::makeError(caudio::utils::Result::Corrupt,
                                                   sqlite3_errmsg(db_));
                    ok = false;
                }
                // stmt finalized by unique_ptr destructor
            } else if (!op.sql.empty() && db_) {
                char* e = nullptr;
                internal::SqliteErrGuard guard{e};
                int rc = sqlite3_exec(db_, op.sql.c_str(), nullptr, nullptr, &e);
                if (rc != SQLITE_OK) {
                    err = caudio::utils::makeError(caudio::utils::Result::Corrupt,
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

    std::unique_ptr<caudio::utils::MpscQueue<WriteOp>> queue_;
    sqlite3* db_{nullptr};
    std::jthread thread_{};
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<int> in_flight_{0};
};

} // namespace caudio::db
