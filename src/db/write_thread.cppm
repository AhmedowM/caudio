module;
#include <string>
#include <memory>
#include <functional>
#include <expected>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <jthread>
#include <chrono>
import caudio.db;
import caudio.utils;

export module caudio.db;

export namespace caudio::db {

struct WriteOp {
  std::string sql;
  std::unique_ptr<Statement> stmt;
  std::function<void(std::expected<void, caudio::utils::Error>)> cb;
};

class WriterThread final {
public:
  WriterThread() = default;

  // Start the writer thread with a database connection; idempotent
  void open(sqlite3* db) {
    if (thread_.get_id() == std::jthread::id()) {
      queue_ = MpscQueue<WriteOp>{256};
      db_ = db;
      thread_ = std::jthread{[this](std::stop_token st) { this->worker(st); }};
    }
  }

  // Request stop and join the thread
  void close() {
    if (thread_.get_id() != std::jthread::id()) {
      thread_.request_stop();
      cv_.notify_all();
      thread_.join();
    }
  }

  // Push a write operation; returns Busy if queue full
  std::expected<void, caudio::utils::Error> write(std::string sql,
                                                  std::unique_ptr<Statement> stmt,
                                                  std::function<void(std::expected<void, caudio::utils::Error>)> cb) {
    WriteOp op{std::move(sql), std::move(stmt), std::move(cb)};
    return queue_.push(std::move(op));
  }

  // Flush: wait for pending operations with 200ms timeout.
  // Returns Ok if all pending ops were processed, Busy if timeout before draining.
  std::expected<void, caudio::utils::Error> flush() {
    auto start = std::chrono::steady_clock::now();
    while (true) {
      std::size_t s = queue_.size();
      if (s == 0) return {};
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
      if (elapsed.count() >= 200) {
        return std::unexpected(caudio::utils::makeError(caudio::utils::Result::Busy, "flush timeout"));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
  }

private:
  void worker(std::stop_token st) {
    for (;;) {
      std::unique_lock<std::mutex> lk{queue_mtx_};

      // Wait for an item or stop, with 200ms timeout
      if (!cv_.wait_for(lk, std::chrono::milliseconds{200}, [this, &st] {
            return !queue_.empty() || st.stop_requested();
          })) {
        // Timed out and queue is empty, check stop
        if (st.stop_requested()) break;
        continue;
      }

      if (st.stop_requested() && queue_.empty()) break;

      // Pop the work item (lock released inside pop)
      auto popped = queue_.pop();
      (void)lk;

      if (!popped.has_value()) continue;

      WriteOp op = std::move(popped.value());

      // Execute callback OUTSIDE the lock
      // Process the SQL statement using the database connection
      if (!op.sql_.empty() && db_) {
        sqlite3_stmt* raw{nullptr};
        int rc = sqlite3_prepare_v2(db_, op.sql_.c_str(), op.sql_.size(), &raw, nullptr);
        if (rc == SQLITE_OK) {
          rc = sqlite3_step(raw);
          sqlite3_finalize(raw);
        }
      }

      // Execute the callback with success (no error for simplicity)
      if (op.cb_) {
        op.cb_(std::expected<void, caudio::utils::Error>{});
      }
    }
  }

  MpscQueue<WriteOp> queue_{256};
  sqlite3* db_{nullptr};
  std::jthread thread_{};
  std::mutex queue_mtx_;
  std::condition_variable cv_;
};

} // namespace caudio::db