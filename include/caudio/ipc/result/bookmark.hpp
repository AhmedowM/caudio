#pragma once

/**
 * @file bookmark.hpp
 * @brief Bookmark results for the caudio IPC protocol.
 * @ingroup caudio_ipc
 */

#include <caudio/types/record.hpp>
#include <vector>

namespace caudio::ipc {

// ---------------------------------------------------------------------------
// Bookmark Results
// ---------------------------------------------------------------------------

/**
 * @brief Single bookmark entry.
 * @ingroup caudio_ipc
 * @details Same-type alias of `caudio::types::Bookmark`; kept for IPC source
 * compatibility. JSON shape is unchanged.
 *
 * @json_example {"type": "BookmarkEntry", "id": 1, "track_id": 123, "position_ms": 1234,
 * "note": "intro", "created": 1699999999}
 *
 * @param id Bookmark id.
 * @param track_id Bookmarked track id.
 * @param position_ms Saved position in milliseconds.
 * @param note Optional note.
 * @param created Creation timestamp (seconds since epoch).
 */
using BookmarkEntry = caudio::types::Bookmark;

/**
 * @struct Bookmarks
 * @brief List of bookmarks (returned by BookmarkList).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "Bookmarks", "entries": [...]}
 *
 * @param entries Vector of bookmark entries.
 */
struct Bookmarks final {
    /**
     * @brief Vector of bookmark entries.
     */
    std::vector<BookmarkEntry> entries{};
};

} // namespace caudio::ipc
