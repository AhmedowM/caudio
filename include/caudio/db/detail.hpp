#pragma once
/**
 * @file detail.hpp
 * @brief Internal re-export hub for db helpers.
 * @ingroup caudio_db
 * @details Re-exports fingerprint, fts and stmt_helpers into the
 * `caudio::db::internal` namespace. Required by public db headers with
 * inline code (db_core, scan, search, json, queue, transaction,
 * write_thread); keep installed alongside them.
 */

#include <caudio/db/fingerprint.hpp>
#include <caudio/db/fts.hpp>
#include <caudio/db/stmt_helpers.hpp>
