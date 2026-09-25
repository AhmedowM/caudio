#pragma once
/**
 * @file detail.hpp
 * @brief Internal re-export hub for db helpers.
 * @ingroup caudio_db
 * @details Re-exports :fingerprint, :fts and :stmt_helpers into the
 * `caudio::db::internal` namespace for use by other partitions.
 * Not part of the public API.
 */

#include <caudio/db/fingerprint.hpp>
#include <caudio/db/fts.hpp>
#include <caudio/db/stmt_helpers.hpp>
