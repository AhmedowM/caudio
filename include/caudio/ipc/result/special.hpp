#pragma once

/**
 * @file special.hpp
 * @brief Empty and error result types for the caudio IPC protocol.
 * @ingroup caudio_ipc
 */

#include <caudio/utils/error.hpp>
#include <variant>

namespace caudio::ipc {

// ---------------------------------------------------------------------------
// Special Result Types
// ---------------------------------------------------------------------------

/**
 * @brief Empty result (success with no data) - uses std::monostate.
 * @ingroup caudio_ipc
 */
using Empty = std::monostate;

/**
 * @brief Error type for CLI error responses.
 * @ingroup caudio_ipc
 *
 * @see caudio::utils::Error
 */
using CliError = caudio::utils::Error;

} // namespace caudio::ipc
