module;
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

export module caudio.service:ipc_channel;

import caudio.utils;

export namespace caudio::service {

class IpcChannel {
public:
    virtual ~IpcChannel() = default;
    virtual caudio::utils::Expected<void> send(std::span<const std::byte> data) = 0;
    virtual caudio::utils::Expected<std::vector<std::byte>> recv() = 0;
    virtual void close() noexcept = 0;
};

caudio::utils::Expected<std::string> socketPathFor(const std::filesystem::path& dbPath) {
    try {
        std::string input = dbPath.generic_string();
        if (input.empty()) {
            input = dbPath.string();
        }
        std::size_t raw = std::hash<std::string>{}(input);
        std::uint32_t hv = static_cast<std::uint32_t>(raw & 0xFFFFFFFFu);
        hv ^= static_cast<std::uint32_t>((raw >> 32) & 0xFFFFFFFFu);

        std::array<char, 9> hexBuf{};
        constexpr char kHex[] = "0123456789abcdef";
        for (int i = 7; i >= 0; --i) {
            hexBuf[static_cast<std::size_t>(i)] = kHex[hv & 0xFu];
            hv >>= 4;
        }
        std::string hex(hexBuf.data(), 8);

#ifdef _WIN32
        std::string pipe = std::string("\\\\.\\pipe\\caudio-") + hex;
        return pipe;
#else
        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0') {
            home = std::getenv("USERPROFILE");
        }
        std::filesystem::path base;
        if (home && home[0] != '\0') {
            base = std::filesystem::path(home) / ".local" / "share" / "caudio";
        } else {
            std::error_code ec;
            base = std::filesystem::temp_directory_path(ec) / "caudio";
            if (ec) {
                base = std::filesystem::path("/tmp/caudio");
            }
        }
        std::filesystem::path full = base / ("caudio-" + hex + ".sock");
        return full.generic_string();
#endif
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, e.what())};
    } catch (...) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "socketPathFor failed")};
    }
}

// framing helpers shared by channel and protocol
inline std::vector<std::byte> frameMessage(std::span<const std::byte> payload) {
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

inline caudio::utils::Expected<std::vector<std::byte>> deframeMessage(
    std::span<const std::byte> framed) {
    if (framed.size() < 4) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "frame too small")};
    }
    std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(framed[0])) << 24) |
                        (static_cast<std::uint32_t>(std::to_underlying(framed[1])) << 16) |
                        (static_cast<std::uint32_t>(std::to_underlying(framed[2])) << 8) |
                        static_cast<std::uint32_t>(std::to_underlying(framed[3]));
    if (framed.size() - 4 < len) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, "frame length mismatch")};
    }
    std::vector<std::byte> out;
    out.reserve(len);
    out.insert(out.end(), framed.begin() + 4, framed.begin() + 4 + len);
    return out;
}

} // namespace caudio::service
