// TODO(Audit Directive 2, Appendix C §2.2): split umbrella — IPC parts → include/caudio/ipc/*, config → include/caudio/config.hpp. Keep include/cli/cli.hpp as deprecated shim for one release.
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
