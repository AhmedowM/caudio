module;
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <functional>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <thread>

export module caudio.db:write_thread;

import caudio.utils;

export namespace caudio::db {

struct WriteOp {
    std::string sql;
    sqlite3_stmt *stmt{nullptr};
    std::function<void(std::expected<void, caudio::utils::Error>)> cb;

    WriteOp() = default;
    WriteOp(std::string s, sqlite3_stmt *st,
            std::function<void(std::expected<void, caudio::utils::Error>)> c)
        : sql(std::move(s)), stmt(st), cb(std::move(c)) {}
    WriteOp(const WriteOp &) = delete;
    WriteOp &operator=(const WriteOp &) = delete;
    WriteOp(WriteOp &&o) noexcept : sql(std::move(o.sql)), stmt(o.stmt), cb(std::move(o.cb)) {
        o.stmt = nullptr;
    }
    WriteOp &operator=(WriteOp &&o) noexcept {
        if (this != &o) {
            sql = std::move(o.sql);
            if (stmt)
                sqlite3_finalize(stmt);
            stmt = o.stmt;
            o.stmt = nullptr;
            cb = std::move(o.cb);
        }
        return *this;
    }
    ~WriteOp() { /* stmt finalized in worker, not here; avoid double finalize */ }
};

class WriterThread final {
  public:
    WriterThread() = default;
    ~WriterThread() {
        close();
    }

    WriterThread(const WriterThread &) = delete;
    WriterThread &operator=(const WriterThread &) = delete;

    void open(sqlite3 *db) {
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
        // drain remaining stmts
        while (true) {
            auto v = queue_.pop();
            if (!v)
                break;
            if (v->stmt)
                sqlite3_finalize(v->stmt);
        }
    }

    std::expected<void, caudio::utils::Error>
    push(std::string sql, sqlite3_stmt *stmt,
         std::function<void(std::expected<void, caudio::utils::Error>)> cb) {
        WriteOp op{std::move(sql), stmt, std::move(cb)};
        auto r = queue_.push(std::move(op));
        if (!r)
            return std::unexpected{r.error()};
        {
            std::lock_guard<std::mutex> lk(mtx_);
            (void)lk;
        }
        cv_.notify_one();
        return {};
    }

    // compat overload for old stub: push from WriteOp object
    std::expected<void, caudio::utils::Error>
    write(std::string sql, std::unique_ptr<void, void (*)(void *)> /*stmt*/,
          std::function<void(std::expected<void, caudio::utils::Error>)> cb) {
        (void)sql;
        (void)cb;
        return {};
    }

    std::expected<void, caudio::utils::Error> flush() {
        auto start = std::chrono::steady_clock::now();
        while (true) {
            std::size_t s = queue_.size();
            int flight = in_flight_.load(std::memory_order_acquire);
            if (s == 0 && flight == 0)
                return {};
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
            if (elapsed.count() >= 200) {
                // re-check
                s = queue_.size();
                flight = in_flight_.load(std::memory_order_acquire);
                if (s == 0 && flight == 0)
                    return {};
                return std::unexpected{
                    caudio::utils::makeError(caudio::utils::Result::Busy, "flush timeout")};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    bool empty() const {
        return queue_.empty();
    }
    std::size_t size() const {
        return queue_.size();
    }

  private:
    void worker(std::stop_token st) {
        while (!st.stop_requested()) {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds{200},
                         [this, &st] { return !queue_.empty() || st.stop_requested(); });
            if (st.stop_requested() && queue_.empty())
                break;
            // pop all available up to batch? single for now; don't hold lock during exec
            lk.unlock();
            auto popped = queue_.pop();
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
                int rc = sqlite3_step(op.stmt);
                if (rc != SQLITE_DONE && rc != SQLITE_ROW && rc != SQLITE_OK) {
                    err = caudio::utils::makeError(caudio::utils::Result::Corrupt,
                                                   sqlite3_errmsg(db_));
                    ok = false;
                }
                sqlite3_finalize(op.stmt);
                op.stmt = nullptr;
            } else if (!op.sql.empty() && db_) {
                char *e = nullptr;
                int rc = sqlite3_exec(db_, op.sql.c_str(), nullptr, nullptr, &e);
                if (rc != SQLITE_OK) {
                    err = caudio::utils::makeError(caudio::utils::Result::Corrupt,
                                                   e ? e : sqlite3_errmsg(db_));
                    ok = false;
                }
                if (e)
                    sqlite3_free(e);
            }
            in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            // callback outside lock
            if (op.cb) {
                if (ok)
                    op.cb(std::expected<void, caudio::utils::Error>{});
                else
                    op.cb(std::unexpected{err});
            }
        }
    }

    caudio::utils::MpscQueue<WriteOp> queue_{256};
    sqlite3 *db_{nullptr};
    std::jthread thread_{};
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<int> in_flight_{0};
};

} // namespace caudio::db
