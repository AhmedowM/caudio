module;
#include <string_view>

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
import :detail;
import :fingerprint;
import :fts;
import :stmt_helpers;

import caudio.utils;

/**
 * @brief Public namespace for all database APIs.
 * @ingroup caudio_db
 */
export namespace caudio::db {} // namespace caudio::db
