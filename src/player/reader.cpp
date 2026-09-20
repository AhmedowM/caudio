#include "caudio/player/reader.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace caudio::player {

caudio::utils::Expected<std::unique_ptr<Reader>> FileReader::open(
    const std::filesystem::path& path) {
    if (path.empty()) {
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("empty path")});
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
            caudio::utils::Error{caudio::utils::StatusCode::NotFound, std::string_view("cannot open file")});
    }
    if (detail::fseek64(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::Internal, std::string_view("fseek failed")});
    }
    int64_t sz = detail::ftell64(f);
    if (sz < 0) {
        std::fclose(f);
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::Internal, std::string_view("ftell failed")});
    }
    if (detail::fseek64(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::Internal, std::string_view("fseek failed")});
    }
    // Wrap immediately in unique_ptr so FILE* is owned even if Expected construction throws
    auto holder = std::unique_ptr<FileReader>(new FileReader(f));
    holder->fileSize_ = sz;
    std::unique_ptr<Reader> base = std::move(holder);
    return caudio::utils::Expected<std::unique_ptr<Reader>>{std::move(base)};
}

FileReader::~FileReader() {
    if (file_)
        std::fclose(file_);
}

std::size_t FileReader::read(std::span<std::byte> dst) {
    if (!file_ || dst.empty())
        return 0;
    return std::fread(dst.data(), 1, dst.size(), file_);
}

caudio::utils::Expected<void> FileReader::seek(int64_t offset, int whence) {
    if (!file_)
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("null file")});
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("bad whence")});
    }
    std::lock_guard<std::mutex> lock(seekMutex_);
    int64_t cur = detail::ftell64(file_);
    if (cur < 0)
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::Internal, std::string_view("ftell failed")});
    int64_t sz = fileSize_;
    if (sz < 0)
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::Internal, std::string_view("size not cached")});
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
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("bad whence")});
    }
    int64_t newPos = base + offset;
    if (newPos < 0 || newPos > sz) {
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("seek out of range")});
    }
    if (detail::fseek64(file_, newPos, SEEK_SET) != 0) {
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::Internal, std::string_view("fseek failed")});
    }
    return {};
}

int64_t FileReader::tell() noexcept {
    if (!file_)
        return -1;
    std::lock_guard<std::mutex> lock(seekMutex_);
    return detail::ftell64(file_);
}

int64_t FileReader::size() noexcept {
    return fileSize_;
}

FileReader::FileReader(FILE* f) : file_(f) {}

caudio::utils::Expected<std::unique_ptr<Reader>> MemoryReader::open(
    std::span<const std::byte> data) {
    // copy data to owned buffer — wrap immediately for exception safety
    auto holder = std::unique_ptr<MemoryReader>(new MemoryReader(data));
    std::unique_ptr<Reader> base = std::move(holder);
    return caudio::utils::Expected<std::unique_ptr<Reader>>{std::move(base)};
}

caudio::utils::Expected<std::unique_ptr<Reader>> MemoryReader::open(
    const std::vector<std::byte>& data) {
    return open(std::span<const std::byte>(data.data(), data.size()));
}

caudio::utils::Expected<std::unique_ptr<Reader>> MemoryReader::open(const std::byte* data,
                                                                     std::size_t len) {
    if (len > 0 && data == nullptr) {
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("null data")});
    }
    return open(std::span<const std::byte>(data, len));
}

std::size_t MemoryReader::read(std::span<std::byte> dst) {
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

caudio::utils::Expected<void> MemoryReader::seek(int64_t offset, int whence) {
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("bad whence")});
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
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("bad whence")});
    }
    int64_t newPos = base + offset;
    if (newPos < 0 || newPos > static_cast<int64_t>(buf_.size())) {
        return std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::InvalidArg, std::string_view("seek out of range")});
    }
    pos_ = static_cast<std::size_t>(newPos);
    return {};
}

int64_t MemoryReader::tell() noexcept {
    return static_cast<int64_t>(pos_);
}

int64_t MemoryReader::size() noexcept {
    return static_cast<int64_t>(buf_.size());
}

MemoryReader::MemoryReader(std::span<const std::byte> src) : buf_(src.begin(), src.end()), pos_(0) {}

} // namespace caudio::player
