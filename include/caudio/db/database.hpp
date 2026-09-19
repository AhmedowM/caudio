#pragma once
/**
 * @file database.hpp
 * @brief Aggregate include for the caudio.db module.
 * @ingroup caudio_db
 * @details Includes all public partitions:
 * :types, :schema, :core, :queue, :scan, :search, :json and
 * :write_thread. Internal partitions :detail, :fingerprint, :fts and
 * :stmt_helpers are available separately but not included here.
 */

#include "caudio/db/db_types.hpp"
#include "caudio/db/schema.hpp"
#include "caudio/db/db_core.hpp"
#include "caudio/db/queue.hpp"
#include "caudio/db/scan.hpp"
#include "caudio/db/search.hpp"
#include "caudio/db/json.hpp"
#include "caudio/db/write_thread.hpp"

#include "caudio/utils/utils.hpp"

/**
 * @brief Public namespace for all database APIs.
 * @ingroup caudio_db
 */
namespace caudio::db {}