#pragma once
/**
 * @file cli.hpp
 * @brief Umbrella header for caudio.cli — CLI IPC protocol.
 * @ingroup caudio_cli
 *
 * Aggregates all partitions:
 * - shared/command: Command variant and tag structs
 * - shared/result: Result variant and response structs
 * - shared/protocol: JSON serialization, framing, IpcRequest/IpcReply
 * - config: Config paths and load/save helpers
 */

#include "caudio/db/database.hpp"
#include "caudio/engine/engine.hpp"
#include "caudio/json/json.hpp"
#include "caudio/utils/utils.hpp"
#include "cli/config.hpp"
#include "cli/shared/command.hpp"
#include "cli/shared/protocol.hpp"
#include "cli/shared/result.hpp"

namespace caudio::cli {} // namespace caudio::cli
