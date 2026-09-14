module;
/**
 * @file detail.cppm
 * @brief Internal re-export hub for db helpers.
 * @ingroup caudio_db
 * @details Re-exports :fingerprint, :fts and :stmt_helpers into the
 * `caudio::db::internal` namespace for use by other partitions.
 * Not part of the public API.
 */
module caudio.db:detail;

import :fingerprint;
import :fts;
import :stmt_helpers;
