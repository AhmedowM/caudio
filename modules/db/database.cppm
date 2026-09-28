module;
#include <caudio/db/db_types.hpp>
#include <caudio/db/write_thread.hpp>
#include <string_view>

/**
 * @file database.cppm
 * @brief Aggregate re-export for the caudio.db module.
 * @ingroup caudio_db
 * @details Thin wrapper that re-exports all public partitions:
 * :types, :core, :scan, :search, :json and :write_thread.
 * Statement/transaction/queue/schema/detail internals live in src/db/
 * and have no partitions.
 */

export module caudio.db;

export import :types;
export import :core;
export import :scan;
export import :search;
export import :json;
export import :write_thread;

import caudio.utils;

/**
 * @brief Public namespace for all database APIs.
 * @ingroup caudio_db
 */
export namespace caudio::db {
using ::caudio::db::Bookmark;
using ::caudio::db::DbStats;
using ::caudio::db::HistoryEntry;
using ::caudio::db::HistoryQuery;
using ::caudio::db::Library;
using ::caudio::db::LibraryStatsDetailedData;
using ::caudio::db::Playlist;
using ::caudio::db::Queue;
using ::caudio::db::QueueItem;
using ::caudio::db::Track;
using ::caudio::db::TrackQuery;
using ::caudio::db::WriteOp;
using ::caudio::db::WriterThread;
} // namespace caudio::db
