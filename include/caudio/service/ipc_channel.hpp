/**
 * @file ipc_channel.hpp
 * @brief Byte-transport abstraction for daemon IPC (socket / named pipe).
 * @ingroup caudio_service
 * @details `IpcChannel` is the virtual transport both ends speak through;
 * `frameMessage`/`deframeMessage` add the length prefix shared with the
 * `caudio.ipc` wire framing.
 */
#pragma once

#include <caudio/utils/error.hpp>
#include <cstddef>
#include <span>
#include <vector>

namespace caudio::service {

/**
 * @brief Bidirectional byte transport (Unix socket or Windows named pipe).
 * @ingroup caudio_service
 * @details Implementations blocking-send/receive whole framed messages;
 * see `IpcServer` (accept side) and `IpcClient` (dial side).
 */
class IpcChannel {
  public:
    virtual ~IpcChannel() = default;
    /**
     * @brief Sends one message.
     * @ingroup caudio_service
     * @param data Bytes to write.
     * @return Success or `Io` Error.
     */
    virtual caudio::utils::Expected<void> send(std::span<const std::byte> data) = 0;
    /**
     * @brief Receives one message (blocks).
     * @ingroup caudio_service
     * @return Message bytes or `Io` Error.
     */
    virtual caudio::utils::Expected<std::vector<std::byte>> recv() = 0;
    /**
     * @brief Closes the transport (idempotent).
     * @ingroup caudio_service
     */
    virtual void close() noexcept = 0;
};

/**
 * @brief Prefixes a payload with its 4-byte big-endian length.
 * @ingroup caudio_service
 * @param payload Raw message bytes.
 * @return Framed bytes, same layout as `caudio::ipc::frame`.
 */
std::vector<std::byte> frameMessage(std::span<const std::byte> payload);

/**
 * @brief Strips the length prefix, validating completeness.
 * @ingroup caudio_service
 * @param framed Framed buffer.
 * @return Payload bytes or `InvalidArg` Error when short/incomplete.
 */
caudio::utils::Expected<std::vector<std::byte>> deframeMessage(std::span<const std::byte> framed);

} // namespace caudio::service
