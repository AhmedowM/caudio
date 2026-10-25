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
    std::vector<types::Bookmark> entries{};
};

} // namespace caudio::ipc
