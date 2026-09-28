#pragma once
/**
 * @file db.hpp
 * @brief Aggregate include for the caudio.db module.
 * @ingroup caudio_db
 * @details Includes all public headers: types, core, scan, search, json
 * and write_thread. Statement/transaction/queue/schema/detail internals
 * live in src/db/ and are NOT installed.
 */

#include <caudio/db/db_core.hpp>
#include <caudio/db/db_types.hpp>
#include <caudio/db/json.hpp>
#include <caudio/db/scan.hpp>
#include <caudio/db/search.hpp>
#include <caudio/db/write_thread.hpp>
#include <caudio/utils.hpp>

/**
 * @brief Public namespace for all database APIs.
 * @ingroup caudio_db
 */
namespace caudio::db {}
