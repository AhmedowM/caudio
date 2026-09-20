#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "caudio/utils/utils.hpp"

namespace caudio::service {

class IpcChannel {
  public:
    virtual ~IpcChannel() = default;
    virtual caudio::utils::Expected<void> send(std::span<const std::byte> data) = 0;
    virtual caudio::utils::Expected<std::vector<std::byte>> recv() = 0;
    virtual void close() noexcept = 0;
};

caudio::utils::Expected<std::string> socketPathFor(const std::filesystem::path& dbPath);

// framing helpers shared by channel and protocol
std::vector<std::byte> frameMessage(std::span<const std::byte> payload);

caudio::utils::Expected<std::vector<std::byte>>
deframeMessage(std::span<const std::byte> framed);

} // namespace caudio::service