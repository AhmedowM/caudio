#include "caudio/service/ipc_channel.hpp"

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
#include "caudio/config.hpp"

namespace caudio::service {

caudio::utils::Expected<std::string> socketPathFor(const std::filesystem::path& dbPath) {
    return caudio::cli::socketPathFor(dbPath);
}

std::vector<std::byte> frameMessage(std::span<const std::byte> payload) {
    std::vector<std::byte> out;
    out.reserve(4 + payload.size());
    std::uint32_t len = static_cast<std::uint32_t>(payload.size());
    std::array<std::byte, 4> hdr{
        static_cast<std::byte>((len >> 24) & 0xFF),
        static_cast<std::byte>((len >> 16) & 0xFF),
        static_cast<std::byte>((len >> 8) & 0xFF),
        static_cast<std::byte>(len & 0xFF),
    };
    out.insert(out.end(), hdr.begin(), hdr.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

caudio::utils::Expected<std::vector<std::byte>> deframeMessage(std::span<const std::byte> framed) {
    if (framed.size() < 4) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "frame too small")};
    }
    std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(framed[0])) << 24) |
                        (static_cast<std::uint32_t>(std::to_underlying(framed[1])) << 16) |
                        (static_cast<std::uint32_t>(std::to_underlying(framed[2])) << 8) |
                        static_cast<std::uint32_t>(std::to_underlying(framed[3]));
    if (framed.size() - 4 < len) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "frame length mismatch")};
    }
    std::vector<std::byte> out;
    out.reserve(len);
    out.insert(out.end(), framed.begin() + 4, framed.begin() + 4 + len);
    return out;
}

} // namespace caudio::service
