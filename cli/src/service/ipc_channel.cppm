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
    // Canonical implementation lives in caudio.cli:config — duplicated here to avoid
    // GCC modules cycle (service -> cli import causes GCM corruption with libstdc++ string_view).
    // Keep in sync with caudio::cli::socketPathFor via shared detail_paths logic (copy).
    try {
        std::string input = dbPath.generic_string();
        if (input.empty()) input = dbPath.string();
        std::size_t raw = std::hash<std::string>{}(input);
        std::uint32_t hv = static_cast<std::uint32_t>(raw & 0xFFFFFFFFu);
        hv ^= static_cast<std::uint32_t>((raw >> 32) & 0xFFFFFFFFu);
        constexpr char kHex[] = "0123456789abcdef";
        std::array<char, 9> buf{};
        for (int i = 7; i >= 0; --i) {
            buf[static_cast<std::size_t>(i)] = kHex[hv & 0xFu];
            hv >>= 4;
        }
        std::string hex(buf.data(), 8);
#ifdef _WIN32
        return std::string("\\\\.\\pipe\\caudio-") + hex;
#else
        const char* xdgRuntime = std::getenv("XDG_RUNTIME_DIR");
        std::filesystem::path base;
        if (xdgRuntime && xdgRuntime[0] != '\0') {
            base = std::filesystem::path(xdgRuntime) / "caudio";
        } else {
            const char* xdgData = std::getenv("XDG_DATA_HOME");
            if (xdgData && xdgData[0] != '\0') {
                base = std::filesystem::path(xdgData) / "caudio";
            } else {
                const char* home = std::getenv("HOME");
                if (!home || home[0] == '\0') home = std::getenv("USERPROFILE");
                if (home && home[0] != '\0') {
                    base = std::filesystem::path(home) / ".local" / "share" / "caudio";
                } else {
                    std::error_code ec;
                    base = std::filesystem::temp_directory_path(ec) / "caudio";
                    if (ec) base = std::filesystem::path("/tmp/caudio");
                }
            }
        }
        return (base / ("caudio-" + hex + ".sock")).generic_string();
#endif
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, e.what())};
    } catch (...) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, "socketPathFor failed")};
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
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "frame too small")};
    }
    std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(framed[0])) << 24) |
                        (static_cast<std::uint32_t>(std::to_underlying(framed[1])) << 16) |
                        (static_cast<std::uint32_t>(std::to_underlying(framed[2])) << 8) |
                        static_cast<std::uint32_t>(std::to_underlying(framed[3]));
    if (framed.size() - 4 < len) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "frame length mismatch")};
    }
    std::vector<std::byte> out;
    out.reserve(len);
    out.insert(out.end(), framed.begin() + 4, framed.begin() + 4 + len);
    return out;
}

} // namespace caudio::service




