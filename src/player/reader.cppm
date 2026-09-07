module;
#if defined(_WIN32)
#include <cstdio>
#else
#include <cstdio>
#endif
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module caudio.player:reader;

import caudio.utils;

export namespace caudio::player {

// Abstract Reader — mirrors ca_reader.c interface
class Reader {
  public:
    virtual ~Reader() = default;
    virtual std::size_t read(std::span<std::byte> dst) = 0;
    [[nodiscard]] virtual caudio::utils::Expected<void> seek(int64_t offset, int whence) = 0;
    [[nodiscard]] virtual int64_t tell() noexcept = 0;
    [[nodiscard]] virtual int64_t size() noexcept = 0;
    // NOTE: `data()` removed to avoid GCC 14.2 module ICE.
    // Decoders needing direct memory access (e.g., Vorbis) should use MemoryReader.
};

// 64-bit helpers like ca_reader.c:28
namespace detail {
inline int64_t ftell64(FILE* f) noexcept {
#if defined(_WIN32)
    return _ftelli64(f);
#else
    off_t o = ftello(f);
    if (o == (off_t)-1)
        return -1;
    return static_cast<int64_t>(o);
#endif
}
inline int fseek64(FILE* f, int64_t off, int whence) noexcept {
#if defined(_WIN32)
    return _fseeki64(f, off, whence);
#else
    return fseeko(f, static_cast<off_t>(off), whence);
#endif
}
inline int64_t fileSizeInner(FILE* f) noexcept {
    int64_t cur = ftell64(f);
    if (cur < 0)
        return -1;
    if (fseek64(f, 0, SEEK_END) != 0)
        return -1;
    int64_t sz = ftell64(f);
    if (sz < 0)
        return -1;
    if (fseek64(f, cur, SEEK_SET) != 0)
        return -1;
    return sz;
}
} // namespace detail

class FileReader final : public Reader {
  public:
    FileReader() = delete;

    static caudio::utils::Expected<std::unique_ptr<Reader>>
    open(const std::filesystem::path& path) {
        if (path.empty()) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "empty path"});
        }
        FILE* f = nullptr;
#if defined(_WIN32)
        f = _wfopen(path.wstring().c_str(), L"rb");
        if (!f) {
            // fallback to narrow
            std::string s = path.string();
            f = std::fopen(s.c_str(), "rb");
        }
#else
        std::string s = path.string();
        f = std::fopen(s.c_str(), "rb");
#endif
        if (!f) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::NotFound, "cannot open file"});
        }
        if (detail::fseek64(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::Internal, "fseek failed"});
        }
        int64_t sz = detail::ftell64(f);
        if (sz < 0) {
            std::fclose(f);
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::Internal, "ftell failed"});
        }
        if (detail::fseek64(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::Internal, "fseek failed"});
        }
        // Wrap immediately in unique_ptr so FILE* is owned even if Expected construction throws
        auto holder = std::unique_ptr<FileReader>(new FileReader(f));
        holder->fileSize_ = sz;
        std::unique_ptr<Reader> base = std::move(holder);
        return caudio::utils::Expected<std::unique_ptr<Reader>>{std::move(base)};
    }

    ~FileReader() override {
        if (file_)
            std::fclose(file_);
    }

    std::size_t read(std::span<std::byte> dst) override {
        if (!file_ || dst.empty())
            return 0;
        return std::fread(dst.data(), 1, dst.size(), file_);
    }

    [[nodiscard]] caudio::utils::Expected<void> seek(int64_t offset, int whence) override {
        if (!file_)
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "null file"});
        if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad whence"});
        }
        std::lock_guard<std::mutex> lock(seekMutex_);
        int64_t cur = detail::ftell64(file_);
        if (cur < 0)
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::Internal, "ftell failed"});
        int64_t sz = fileSize_;
        if (sz < 0)
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::Internal, "size not cached"});
        int64_t base = 0;
        switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = cur;
            break;
        case SEEK_END:
            base = sz;
            break;
        default:
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad whence"});
        }
        int64_t newPos = base + offset;
        if (newPos < 0 || newPos > sz) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "seek out of range"});
        }
        if (detail::fseek64(file_, newPos, SEEK_SET) != 0) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::Internal, "fseek failed"});
        }
        return {};
    }

    [[nodiscard]] int64_t tell() noexcept override {
        if (!file_)
            return -1;
        std::lock_guard<std::mutex> lock(seekMutex_);
        return detail::ftell64(file_);
    }

    [[nodiscard]] int64_t size() noexcept override {
        return fileSize_;
    }

private:
    explicit FileReader(FILE* f) : file_(f) {}
    FILE* file_{nullptr};
    int64_t fileSize_{-1};
    mutable std::mutex seekMutex_{};
  };

class MemoryReader final : public Reader {
  public:
    MemoryReader() = delete;

    static caudio::utils::Expected<std::unique_ptr<Reader>> open(std::span<const std::byte> data) {
        // copy data to owned buffer — wrap immediately for exception safety
        auto holder = std::unique_ptr<MemoryReader>(new MemoryReader(data));
        std::unique_ptr<Reader> base = std::move(holder);
        return caudio::utils::Expected<std::unique_ptr<Reader>>{std::move(base)};
    }

    static caudio::utils::Expected<std::unique_ptr<Reader>>
    open(const std::vector<std::byte>& data) {
        return open(std::span<const std::byte>(data.data(), data.size()));
    }

    static caudio::utils::Expected<std::unique_ptr<Reader>> open(const std::byte* data,
                                                                 std::size_t len) {
        if (len > 0 && data == nullptr) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "null data"});
        }
        return open(std::span<const std::byte>(data, len));
    }

    std::size_t read(std::span<std::byte> dst) override {
        if (dst.empty())
            return 0;
        std::size_t avail = buf_.size() > pos_ ? buf_.size() - pos_ : 0;
        std::size_t n = dst.size() < avail ? dst.size() : avail;
        if (n > 0) {
            std::memcpy(dst.data(), buf_.data() + pos_, n);
            pos_ += n;
        }
        return n;
    }

    [[nodiscard]] caudio::utils::Expected<void> seek(int64_t offset, int whence) override {
        if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad whence"});
        }
        int64_t base = 0;
        switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = static_cast<int64_t>(pos_);
            break;
        case SEEK_END:
            base = static_cast<int64_t>(buf_.size());
            break;
        default:
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad whence"});
        }
        int64_t newPos = base + offset;
        if (newPos < 0 || newPos > static_cast<int64_t>(buf_.size())) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "seek out of range"});
        }
        pos_ = static_cast<std::size_t>(newPos);
        return {};
    }

    [[nodiscard]] int64_t tell() noexcept override {
        return static_cast<int64_t>(pos_);
    }
    [[nodiscard]] int64_t size() noexcept override {
        return static_cast<int64_t>(buf_.size());
    }

  private:
    explicit MemoryReader(std::span<const std::byte> src) : buf_(src.begin(), src.end()), pos_(0) {}
    std::vector<std::byte> buf_;
    std::size_t pos_{0};
};

} // namespace caudio::player