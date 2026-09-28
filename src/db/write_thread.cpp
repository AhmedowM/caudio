#include <sqlite3.h>

#include <db/detail.hpp>
#include <db/statement.hpp>
#include <caudio/db/write_thread.hpp>
#include <caudio/utils.hpp>
#include <chrono>

namespace caudio::db {

// Out-of-line: the header only forward-declares SqliteStatement.
WriteOp::WriteOp() = default;

WriteOp::WriteOp(std::string s, std::unique_ptr<SqliteStatement> st,
                 std::move_only_function<void(std::expected<void, caudio::utils::Error>)> c)
    : sql(std::move(s)), stmt(std::move(st)), cb(std::move(c)) {}

WriteOp::WriteOp(WriteOp&&) noexcept = default;
WriteOp& WriteOp::operator=(WriteOp&&) noexcept = default;
WriteOp::~WriteOp() = default;

WriterThread::WriterThread(std::size_t writeBatchSize)
    : queue_(std::make_unique<caudio::utils::MpscQueue<WriteOp>>(writeBatchSize > 0 ? writeBatchSize
                                                                                    : 256)) {}

WriterThread::~WriterThread() {
    close();
}

WriterThread::WriterThread(WriterThread&& o) noexcept
    : queue_(std::move(o.queue_)), db_(std::exchange(o.db_, nullptr)),
      thread_(std::move(o.thread_)), in_flight_(o.in_flight_.load(std::memory_order_acquire)) {}

WriterThread& WriterThread::operator=(WriterThread&& o) noexcept {
    if (this != &o) {
        close();
        queue_ = std::move(o.queue_);
        db_ = std::exchange(o.db_, nullptr);
        thread_ = std::move(o.thread_);
        in_flight_.store(o.in_flight_.load(std::memory_order_acquire), std::memory_order_release);
    }
    return *this;
}

void WriterThread::open(sqlite3* db) {
    if (thread_.joinable())
        return;
    db_ = db;
    thread_ = std::jthread{[this](std::stop_token st) { worker(st); }};
}

void WriterThread::close() {
    if (thread_.joinable()) {
        thread_.request_stop();
        cv_.notify_all();
        thread_.join();
    }
    while (true) {
        auto v = queue_->pop();
        if (!v)
            break;
    }
}

std::expected<void, caudio::utils::Error>
WriterThread::push(std::string sql, std::unique_ptr<SqliteStatement> stmt,
                   std::move_only_function<void(std::expected<void, caudio::utils::Error>)> cb) {
    WriteOp op{std::move(sql), std::move(stmt), std::move(cb)};
    auto r = queue_->push(std::move(op));
    if (!r)
        return std::unexpected{r.error()};
    cv_.notify_one();
    return {};
}

std::expected<void, caudio::utils::Error>
WriterThread::push(std::string sql,
                   std::move_only_function<void(std::expected<void, caudio::utils::Error>)> cb) {
    return push(std::move(sql), std::unique_ptr<SqliteStatement>{}, std::move(cb));
}

std::expected<void, caudio::utils::Error> WriterThread::flush() {
    std::unique_lock<std::mutex> lk(mtx_);
    bool done = cv_.wait_for(lk, std::chrono::milliseconds{200}, [this] {
        return queue_->empty() && in_flight_.load(std::memory_order_acquire) == 0;
    });
    if (done)
        return {};
    if (queue_->empty() && in_flight_.load(std::memory_order_acquire) == 0)
        return {};
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::StatusCode::Busy, "flush timeout")};
}

bool WriterThread::empty() const {
    return queue_->empty();
}

std::size_t WriterThread::size() const {
    return queue_->size();
}

void WriterThread::worker(std::stop_token st) {
    while (!st.stop_requested()) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, std::chrono::milliseconds{200},
                     [this, &st] { return !queue_->empty() || st.stop_requested(); });
        if (st.stop_requested() && queue_->empty())
            break;
        lk.unlock();
        auto popped = queue_->pop();
        if (!popped.has_value()) {
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
        if (op.cb) {
            if (ok)
                op.cb(std::expected<void, caudio::utils::Error>{});
            else
                op.cb(std::unexpected{err});
        }
    }
}

} // namespace caudio::db
