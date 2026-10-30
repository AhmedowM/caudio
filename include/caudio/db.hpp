#pragma once

/**
 * @file db.hpp
 * @brief Aggregate include for the caudio.db module.
 * @ingroup caudio_db
 * @defgroup caudio_db caudio database
 * @details Includes all public headers: types, core, scan, search, json.
 * Statement/transaction/queue/schema/detail internals and the background
 * writer thread live in src/db/ and are NOT installed.
 */

#include <caudio/db/database.hpp>
#include <caudio/db/json.hpp>
#include <caudio/db/scan.hpp>
#include <caudio/db/search.hpp>
#include <caudio/types/db.hpp>
