module;
#include <string_view>

#include "caudio/db/db_types.hpp"
#include "caudio/db/statement.hpp"
#include "caudio/db/transaction.hpp"
#include "caudio/db/write_thread.hpp"

/**
 * @file database.cppm
 * @brief Aggregate re-export for the caudio.db module.
 * @ingroup caudio_db
 * @details Thin wrapper that re-exports all public partitions:
 * :types, :schema, :core, :queue, :scan, :search, :json and
 * :write_thread. Internal partitions :detail, :fingerprint, :fts and
 * :stmt_helpers are imported privately.
 */

export module caudio.db;

export import :types;
export import :schema;
export import :core;
export import :queue;
export import :scan;
export import :search;
export import :json;
export import :write_thread;
export import :SqliteStatement;
export import :DbTransaction;
import :detail;
import :fingerprint;
import :fts;
import :stmt_helpers;

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
