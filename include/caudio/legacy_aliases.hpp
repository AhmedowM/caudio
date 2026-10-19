#pragma once

/**
 * @file legacy_aliases.hpp
 * @brief Deprecated spelling shims for value types moved to `caudio::types`.
 * @ingroup caudio
 * @details Every name below is a same-type alias of its `caudio::types`
 * counterpart, kept only so existing `caudio::db::` / `caudio::engine::`
 * spellings keep compiling during the v1.8.0 deprecation window. The whole
 * file is scheduled for deletion in v2.0.0 (U-6c); do not add new uses --
 * spell the `caudio::types::` name directly. New internal use already fails
 * the build via `-Werror=deprecated-declarations`.
 */

#include <caudio/types/record.hpp>
#include <caudio/types/wire.hpp>

namespace caudio::db {

using Bookmark [[deprecated("use caudio::types::Bookmark")]] = caudio::types::Bookmark;
using HistoryEntry [[deprecated("use caudio::types::HistoryEntry")]] = caudio::types::HistoryEntry;
using Playlist [[deprecated("use caudio::types::Playlist")]] = caudio::types::Playlist;
using QueueItem [[deprecated("use caudio::types::QueueItem")]] = caudio::types::QueueItem;
using Track [[deprecated("use caudio::types::Track")]] = caudio::types::Track;

} // namespace caudio::db

namespace caudio::engine {

using PlaybackState [[deprecated("use caudio::types::PlaybackState")]] =
    caudio::types::PlaybackState;
using RepeatMode [[deprecated("use caudio::types::RepeatMode")]] = caudio::types::RepeatMode;

} // namespace caudio::engine
