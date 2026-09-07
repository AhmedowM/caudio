module;
#include <sqlite3.h>
#include <blake3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

module caudio.db:detail;

import caudio.utils;
import :types;

namespace caudio::db::detail {

// BLAKE3(head 64K ∥ tail 64K ∥ LE64(size) ∥ LE32(ver=1)) sampled
inline constexpr size_t kSample = 64 * 1024;

inline std::expected<std::array<uint8_t, 32>, caudio::utils::Error>
computeFingerprint(const std::filesystem::path& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, ec.message())};
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Io, "cannot open file")};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    std::vector<uint8_t> buf(kSample);
    // head
    f.read(reinterpret_cast<char*>(buf.data()), kSample);
    size_t n = (size_t)f.gcount();
    if (n)
        blake3_hasher_update(&hasher, buf.data(), n);
    // tail if file larger than kSample
    if (sz > kSample) {
        f.clear();
        f.seekg((std::streamoff)(sz - kSample), std::ios::beg);
        if (f) {
            f.read(reinterpret_cast<char*>(buf.data()), kSample);
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

inline constexpr std::string_view kSelectTracksCols =
    "SELECT id, fingerprint, path, deleted_at, size, mtime, duration, sample_rate, channels, "
    "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
    "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, library_id "
    "FROM tracks";

inline std::string escapeLike(std::string_view term) {
    std::string result;
    result.reserve(term.size() + 4);
    for (char c : term) {
        if (c == '%' || c == '_' || c == '\\') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    return result;
}

inline std::string toHex(const std::array<uint8_t, 32>& fp) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (uint8_t b : fp) {
        s.push_back(hex[b >> 4]);
        s.push_back(hex[b & 0xf]);
    }
    return s;
}

inline bool fromHex(std::string_view hexStr, std::array<uint8_t, 32>& out) {
    if (hexStr.size() != 64)
        return false;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < 32; i++) {
        int hi = hv(hexStr[i * 2]);
        int lo = hv(hexStr[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

inline std::string sanitizeFtsTerm(std::string_view term) {
    // Empty term → return empty to match everything
    if (term.empty())
        return {};

    // Strip FTS5 operators and special chars that could cause syntax errors
    // but preserve and escape quotes
    std::string cleaned;
    cleaned.reserve(term.size());
    for (char c : term) {
        if (c == '\'' || c == '*' || c == ':' || c == '-' ||
            c == '(' || c == ')' || c == '^' || c == '~')
            continue;
        if (c == '"') {
            // Escape quote by doubling it for FTS5
            cleaned.push_back('"');
            cleaned.push_back('"');
        } else {
            cleaned.push_back(c);
        }
    }

    // Remove FTS5 logical operators (case-insensitive) - replace with space
    // Handle word boundaries: operator can be at start, middle, or end
    std::string upper = cleaned;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    
    // First handle operators with spaces (middle of string)
    std::string opsWithSpaces[] = {" AND ", " OR ", " NOT ", " NEAR "};
    for (const auto& op : opsWithSpaces) {
        size_t pos = 0;
        while ((pos = upper.find(op, pos)) != std::string::npos) {
            upper.replace(pos, op.size(), std::string(op.size(), ' '));
            cleaned.replace(pos, op.size(), std::string(op.size(), ' '));
            pos += op.size();
        }
    }
    
    // Then handle operators at start (e.g., "NOT foo")
    std::string opsStart[] = {"AND ", "OR ", "NOT ", "NEAR "};
    for (const auto& op : opsStart) {
        if (upper.rfind(op, 0) == 0) {
            upper.replace(0, op.size(), std::string(op.size(), ' '));
            cleaned.replace(0, op.size(), std::string(op.size(), ' '));
        }
    }
    
    // Then handle operators at end (e.g., "foo NOT")
    std::string opsEnd[] = {" AND", " OR", " NOT", " NEAR"};
    for (const auto& op : opsEnd) {
        if (upper.size() >= op.size() && upper.compare(upper.size() - op.size(), op.size(), op) == 0) {
            size_t pos = upper.size() - op.size();
            upper.replace(pos, op.size(), std::string(op.size(), ' '));
            cleaned.replace(pos, op.size(), std::string(op.size(), ' '));
        }
    }

    // Collapse multiple spaces to single space
    std::string result;
    result.reserve(cleaned.size());
    bool lastWasSpace = false;
    for (char c : cleaned) {
        if (c == ' ') {
            if (!lastWasSpace) {
                result.push_back(' ');
                lastWasSpace = true;
            }
        } else {
            result.push_back(c);
            lastWasSpace = false;
        }
    }

    // Trim leading/trailing spaces
    size_t start = 0;
    while (start < result.size() && result[start] == ' ') start++;
    size_t end = result.size();
    while (end > start && result[end - 1] == ' ') end--;
    if (start >= end)
        return {};

    std::string final = result.substr(start, end - start);

    // If nothing left after stripping, return empty
    if (final.empty())
        return {};

    return final;
}

inline void fillTrackFromStmt(sqlite3_stmt* stmt, Track& t) {
    t.id = sqlite3_column_int64(stmt, 0);
    t.fingerprint.fill(0);
    if (sqlite3_column_bytes(stmt, 1) == 32) {
        std::memcpy(t.fingerprint.data(), sqlite3_column_blob(stmt, 1), 32);
    }
    t.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";
    t.deleted_at = sqlite3_column_int64(stmt, 3);
    t.size = sqlite3_column_int64(stmt, 4);
    t.mtime = sqlite3_column_int64(stmt, 5);
    t.duration = sqlite3_column_double(stmt, 6);
    t.sample_rate = static_cast<uint32_t>(sqlite3_column_int(stmt, 7));
    t.channels = static_cast<uint32_t>(sqlite3_column_int(stmt, 8));
    t.bitrate = static_cast<int>(sqlite3_column_int(stmt, 9));
    t.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)) : "";
    t.artist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)) : "";
    t.album = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12)) : "";
    t.albumArtist = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13)) : "";
    t.genre = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14)) : "";
    t.year = static_cast<int>(sqlite3_column_int(stmt, 15));
    t.track_num = static_cast<int>(sqlite3_column_int(stmt, 16));
    t.disc_num = static_cast<int>(sqlite3_column_int(stmt, 17));
    t.cover_art_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 18)) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 18)) : "";
    t.rating = static_cast<int>(sqlite3_column_int(stmt, 19));
    t.play_count = sqlite3_column_int64(stmt, 20);
    t.last_played = sqlite3_column_int64(stmt, 21);
    t.date_added = sqlite3_column_int64(stmt, 22);
    t.last_scanned = sqlite3_column_int64(stmt, 23);
    t.dirty = sqlite3_column_int(stmt, 24) != 0;
    t.library_id = static_cast<int>(sqlite3_column_int(stmt, 25));
}

} // namespace caudio::db::detail