#pragma once

/**
 * @file command.hpp
 * @brief CLI command types and variants for the caudio IPC protocol.
 * @ingroup caudio_ipc
 *
 * Defines all command structs that can be sent from a client to the
 * caudio service daemon. Commands are serialized as JSON over the
 * IPC socket/named pipe connection.
 *
 * ## IPC Protocol Structure
 *
 * Commands are sent as JSON objects with a "type" field identifying the command.
 * The full IPC message format is:
 * - 4-byte big-endian length prefix (uint32_t)
 * - JSON payload containing an IpcRequest with:
 *   - `id`: uint32_t request identifier
 *   - `cmd`: Command object with `type` and command-specific fields
 *
 * @see caudio::ipc::protocol for serialization/deserialization functions
 * @see caudio::ipc::Result for response types
 */

#include <caudio/ipc/command/bookmark.hpp>
#include <caudio/ipc/command/config.hpp>
#include <caudio/ipc/command/device.hpp>
#include <caudio/ipc/command/history.hpp>
#include <caudio/ipc/command/library.hpp>
#include <caudio/ipc/command/playback.hpp>
#include <caudio/ipc/command/playlist.hpp>
#include <caudio/ipc/command/queue.hpp>
#include <caudio/ipc/command/system.hpp>
#include <caudio/ipc/command/tag.hpp>
#include <caudio/ipc/command/variant.hpp>
