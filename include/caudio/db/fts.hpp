#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file fts.hpp
 * @brief FTS5 sanitization, LIKE escaping and hex helpers.
 * @ingroup caudio_db
 * @details Internal helpers: escapeLike() escapes LIKE wildcards,
 * toHex()/fromHex() convert fingerprints, sanitizeFtsTerm() strips
 * FTS5 operators and escapes quotes by doubling them. See search.hpp
 * for the quoting logic and the queue UNIQUE invariant discussion.
 */

namespace caudio::db::internal {

/**
 * @brief Escapes LIKE wildcards % _ and \ with backslash.
 * @ingroup caudio_db
 * @param term Raw term.
 * @return Escaped term suitable for LIKE ... ESCAPE '\'.
 */
std::string escapeLike(std::string_view term);

/**
 * @brief Converts a 32-byte fingerprint to 64-char hex.
 * @ingroup caudio_db
 * @param fp Fingerprint bytes.
 * @return Lowercase hex string.
 */
std::string toHex(const std::array<uint8_t, 32>& fp);

/**
 * @brief Parses 64-char hex into 32 bytes.
 * @ingroup caudio_db
 * @param hexStr Hex view (must be 64 chars).
 * @param out Output bytes.
 * @return true on success, false on length/char error.
 */
bool fromHex(std::string_view hexStr, std::array<uint8_t, 32>& out);

/**
 * @brief Sanitizes a user term for FTS5 MATCH.
 * @ingroup caudio_db
 * @param term Raw user input.
 * @return Sanitized term or empty (match-nothing).
 * @details Strips FTS5 syntax (* : - ( ) ^ ~ '), escapes " by doubling,
 * removes logical operators AND/OR/NOT/NEAR (case-insensitive, word-boundary),
 * collapses spaces and trims. Empty after stripping returns empty.
 */
std::string sanitizeFtsTerm(std::string_view term);

} // namespace caudio::db::internal