#include <caudio/utils/log_sink.hpp>

#include <caudio/utils/print.hpp>
#include <chrono>
#include <cstdio>
#include <format>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace caudio::utils {

namespace {

std::string_view levelTag(LogLevel lvl) noexcept {
    switch (lvl) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO ";
    case LogLevel::Warn:
        return "WARN ";
    case LogLevel::Error:
        return "ERROR";
    default:
        return "?????";
    }
}

} // namespace

std::shared_ptr<FileLogSink> FileLogSink::open(const std::filesystem::path& path,
                                               const FileSinkOptions& opts) {
    auto sink = std::shared_ptr<FileLogSink>(new FileLogSink());
    sink->path_ = path;
    sink->opts_ = opts;
    if (sink->opts_.generations < 1)
        sink->opts_.generations = 1;
    sink->out_.open(path, std::ios::out | std::ios::app);
    if (!sink->out_.is_open())
        return nullptr;
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    sink->currentBytes_ = ec ? 0 : static_cast<std::size_t>(size);
    sink->valid_ = true;
    if (sink->currentBytes_ > sink->opts_.maxBytes) {
        std::lock_guard<std::mutex> lk(sink->mutex_);
        sink->rotate();
    }
    return sink;
}

Logger::Callback FileLogSink::callback() {
    std::shared_ptr<FileLogSink> self = shared_from_this();
    return [self](LogLevel lvl, std::string_view msg) { self->writeLine(lvl, msg); };
}

bool FileLogSink::valid() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return valid_;
}

void FileLogSink::flush() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (valid_)
        out_.flush();
}

std::string FileLogSink::formatLine(LogLevel lvl, std::string_view msg) {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    // Low 24 bits distinguish the daemon's handful of threads; the full
    // 64-bit hash is visual noise in every line.
    const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xffffffu;
    return std::format("[{:%FT%TZ}] [{}] [t:{:06x}] {}\n", now, levelTag(lvl), tid, msg);
}

void FileLogSink::writeLine(LogLevel lvl, std::string_view msg) {
    std::string line;
    try {
        line = formatLine(lvl, msg);
    } catch (...) {
        return;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    if (!valid_)
        return;
    try {
        if (currentBytes_ + line.size() > opts_.maxBytes)
            rotate();
        if (!valid_)
            return;
        out_ << line;
        out_.flush();
        if (!out_)
            valid_ = false;
        else
            currentBytes_ += line.size();
    } catch (...) {
        valid_ = false;
    }
    if (opts_.teeStderr) {
        try {
            caudio::println(std::cerr, "[{}] {}", levelTag(lvl), msg);
        } catch (...) {
        }
    }
}

void FileLogSink::rotate() {
    // Caller holds mutex_. Failures degrade to truncate-and-continue so the
    // bound (generations * maxBytes) always holds.
    try {
        out_.close();
        const int gens = opts_.generations;
        std::error_code ec;
        std::filesystem::remove(indexedPath(gens - 1), ec);
        for (int i = gens - 2; i >= 0; --i)
            std::filesystem::rename(indexedPath(i), indexedPath(i + 1), ec);
        out_.open(path_, std::ios::out | std::ios::trunc);
        valid_ = out_.is_open();
        currentBytes_ = 0;
    } catch (...) {
        valid_ = false;
    }
}

std::filesystem::path FileLogSink::indexedPath(int i) const {
    if (i <= 0)
        return path_;
    return std::filesystem::path(path_.string() + "." + std::to_string(i));
}

} // namespace caudio::utils
