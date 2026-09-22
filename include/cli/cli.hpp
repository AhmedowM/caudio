// TODO(Audit Directive 2, Appendix C Â§2.2): split umbrella â€” IPC parts â†’ include/caudio/ipc/*, config â†’ include/caudio/config.hpp. Keep include/cli/cli.hpp as deprecated shim for one release.
#pragma once
/**
 * @file cli.hpp
 * @brief Umbrella header for caudio.cli â€” CLI IPC protocol.
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
#include "caudio/json.hpp"
#include "caudio/utils/utils.hpp"
#include "caudio/config.hpp"
#include "caudio/ipc/command.hpp"
#include "caudio/ipc/protocol.hpp"
#include "caudio/ipc/result.hpp"

namespace caudio::cli {} // namespace caudio::cli
