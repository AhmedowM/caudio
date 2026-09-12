module;
#include <blake3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

module caudio.db:fingerprint;

import caudio.utils;
import :types;

namespace caudio::db::internal {

// BLAKE3(head 64K || tail 64K || LE64(size) || LE32(ver=1)) sampled
inline constexpr size_t kSample = 64uz * 1024uz;

inline std::expected<std::array<uint8_t, 32>, caudio::utils::Error>
computeFingerprint(const std::filesystem::path& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, ec.message())};
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "cannot open file")};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    thread_local std::array<std::byte, kSample> buf{};
    // head
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(kSample));
    size_t n = (size_t)f.gcount();
    if (n)
        blake3_hasher_update(&hasher, buf.data(), n);
    // tail if file larger than kSample
    if (sz > kSample) {
        f.clear();
        f.seekg((std::streamoff)(sz - kSample), std::ios::beg);
        if (f) {
            f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(kSample));
            n = (size_t)f.gcount();
            if (n)
                blake3_hasher_update(&hasher, buf.data(), n);
        }
    }
    uint64_t sz64 = (uint64_t)sz;
    blake3_hasher_update(&hasher, &sz64, sizeof(sz64));
    uint32_t ver = 1;
    blake3_hasher_update(&hasher, &ver, sizeof(ver));
    std::array<uint8_t, 32> out{};
    blake3_hasher_finalize(&hasher, out.data(), out.size());
    return out;
}

inline std::array<uint8_t, 32> fallbackFingerprint(std::string_view path) noexcept {
    uint64_t h = 1469598103934665603ULL;
    for (char c : path) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ULL;
    }
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 32; i++) {
        out[i] = static_cast<uint8_t>(h >> ((i % 8) * 8));
        h = h * 6364136223846793005ULL + 1;
    }
    return out;
}

} // namespace caudio::db::internal





