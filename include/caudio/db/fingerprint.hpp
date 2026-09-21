#pragma once
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

#include "caudio/db/db_types.hpp"
#include "caudio/utils/utils.hpp"

/**
 * @file fingerprint.hpp
 * @brief Content fingerprinting (BLAKE3 sampled vs full).
 * @ingroup caudio_db
 * @details Fingerprints are 32-byte BLAKE3 digests used as the primary
 * deduplication key (`tracks.fingerprint` UNIQUE). Two modes:
 * - Sampled (default, used by scan): `BLAKE3(head 64 KiB || tail 64 KiB || LE64(size) ||
 * LE32(version=1))`. Fast for large files; stable across metadata-only changes because only audio
 * bytes are hashed (but file-size is mixed in to avoid collisions on truncated files).
 * - Full (used when `ScanMode::Full`): hashes the entire file sequentially.
 * See `scan.hpp` for the full-file path.
 */

namespace caudio::db::internal {

/**
 * @brief Number of bytes sampled from head and tail.
 * @ingroup caudio_db
 * @details 64 KiB each. Files <= 64 KiB hash only once (head == whole file plus size/version
 * trailer).
 */
inline constexpr size_t kSample = 64uz * 1024uz;

/**
 * @brief Computes a sampled BLAKE3 fingerprint for a file.
 * @ingroup caudio_db
 * @param path Filesystem path to hash.
 * @return 32-byte digest on success, or `Error` with `StatusCode::Io` if the file
 * cannot be sized or opened.
 * @details Algorithm: `BLAKE3( head[0..64K) || tail[size-64K..size) || LE64(size) ||
 * LE32(version=1) )`. Uses a thread-local 64 KiB buffer to avoid per-call allocation. The version
 * field allows future fingerprint upgrades without silent collisions.
 * @par Thread safety
 * Thread-safe: uses `thread_local` buffer; no shared state. File I/O is blocking.
 * @see fallbackFingerprint
 * @see kSample
 */
std::expected<std::array<uint8_t, 32>, caudio::utils::Error>
computeFingerprint(const std::filesystem::path& path);

/**
 * @brief Deterministic fallback fingerprint derived from the path string.
 * @ingroup caudio_db
 * @param path Path view to hash (used when file cannot be read or hex parse fails).
 * @return 32-byte pseudo-digest (FNV-1a seeded + LCG expansion).
 * @details Not cryptographically strong; only used to ensure every track has a
 * non-zero UNIQUE fingerprint when I/O fails (e.g., in `trackFromJson()` or
 * `insertTrackLegacy()`). Never used for deduplication of readable files.
 * @par Thread safety
 * Pure function, thread-safe.
 */
std::array<uint8_t, 32> fallbackFingerprint(std::string_view path) noexcept;

} // namespace caudio::db::internal
