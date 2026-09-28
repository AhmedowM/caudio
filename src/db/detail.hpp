#pragma once
/**
 * @file detail.hpp
 * @brief Internal re-export hub for db helpers.
 * @ingroup caudio_db
 * @details Re-exports fingerprint, fts and stmt_helpers into the
 * `caudio::db::internal` namespace. Private to src/ (NOT installed);
 * included by db .cpps and src-private db headers.
 */

#include <db/fingerprint.hpp>
#include <db/fts.hpp>
#include <db/stmt_helpers.hpp>
