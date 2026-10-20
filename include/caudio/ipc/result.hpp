#pragma once

/**
 * @file result.hpp
 * @brief CLI result types and variants for the caudio IPC protocol.
 * @ingroup caudio_ipc
 *
 * Defines all result structs that can be returned from the caudio service
 * daemon to a client. Results are serialized as JSON over the IPC
 * socket/named pipe connection.
 *
 * ## IPC Protocol Structure
 *
 * Results are sent as JSON objects with a "type" field identifying the result type.
 * The full IPC message format is:
 * - 4-byte big-endian length prefix (uint32_t)
 * - JSON payload containing an IpcReply with:
 *   - `id`: uint32_t request identifier (matches request)
 *   - `ok`: boolean indicating success
 *   - `result`: Result object (if ok=true) or `error`: Error object (if ok=false)
 *
 * @see caudio::ipc::protocol for serialization/deserialization functions
 * @see caudio::ipc::Command for request types
 */

#include <caudio/ipc/result/bookmark.hpp>
#include <caudio/ipc/result/config.hpp>
#include <caudio/ipc/result/device.hpp>
#include <caudio/ipc/result/history.hpp>
#include <caudio/ipc/result/library.hpp>
#include <caudio/ipc/result/playback.hpp>
#include <caudio/ipc/result/playlist.hpp>
#include <caudio/ipc/result/queue.hpp>
#include <caudio/ipc/result/special.hpp>
#include <caudio/ipc/result/variant.hpp>
