#pragma once
#include <charconv>
#include <chrono>
#include <cmath>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace caudio::app::parse {

// Use std::string for error message to keep header standalone (no caudio::utils dependency).
// App module wraps errors into caudio::utils::Error.

inline std::expected<double, std::string> parseTime(std::string_view s) {
    // trim whitespace
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    if (s.empty()) return std::unexpected<std::string>{"empty time"};

    // Check for ':' presence -> mm:ss or hh:mm:ss
    size_t colon = s.find(':');
    if (colon != std::string_view::npos) {
        // split by ':'
        double total = 0.0;
        std::string_view remaining = s;
        // Count colons
        size_t count = 0;
        for (char c : s) if (c == ':') ++count;
        // parse each part using from_chars
        double parts[3] = {0, 0, 0};
        int idx = 0;
        while (!remaining.empty() && idx < 3) {
            size_t nxt = remaining.find(':');
            std::string_view token = (nxt == std::string_view::npos) ? remaining : remaining.substr(0, nxt);
            if (token.empty()) return std::unexpected<std::string>{"empty component"};
            // allow fractional for last component only
            double val = 0;
            if (idx == 2 || (count == 1 && idx == 1)) {
                // last component may be double
                auto res = std::from_chars(token.data(), token.data() + token.size(), val);
                if (res.ec != std::errc{} || res.ptr != token.data() + token.size()) {
                    // try integer fallback then parse fractional
                    return std::unexpected<std::string>{"invalid time component"};
                }
            } else {
                int iv = 0;
                auto res = std::from_chars(token.data(), token.data() + token.size(), iv);
                if (res.ec != std::errc{} || res.ptr != token.data() + token.size()) {
                    return std::unexpected<std::string>{"invalid time component"};
                }
                if (iv < 0) return std::unexpected<std::string>{"negative component"};
                val = static_cast<double>(iv);
            }
            parts[idx++] = val;
            if (nxt == std::string_view::npos) break;
            remaining.remove_prefix(nxt + 1);
        }
        if (count == 1) {
            // mm:ss
            double mm = parts[0];
            double ss = parts[1];
            if (ss < 0 || ss >= 60) {
                // allow ss up to <60 but tolerate
            }
            total = mm * 60.0 + ss;
        } else if (count == 2) {
            double hh = parts[0];
            double mm = parts[1];
            double ss = parts[2];
            total = hh * 3600.0 + mm * 60.0 + ss;
        } else {
            return std::unexpected<std::string>{"too many colons"};
        }
        if (!std::isfinite(total) || total < 0) return std::unexpected<std::string>{"invalid time"};
        return total;
    } else {
        double v = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
            return std::unexpected<std::string>{"invalid time"};
        }
        if (!std::isfinite(v) || v < 0) return std::unexpected<std::string>{"invalid time"};
        return v;
    }
}

inline std::expected<double, std::string> parseSeek(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    if (s.empty()) return std::unexpected<std::string>{"empty seek"};
    bool relative = false;
    bool negative = false;
    std::string_view core = s;
    if (core.front() == '+' || core.front() == '-') {
        relative = true;
        negative = (core.front() == '-');
        core.remove_prefix(1);
        if (core.empty()) return std::unexpected<std::string>{"missing seek after sign"};
    }
    auto t = parseTime(core);
    if (!t) return t;
    double v = *t;
    if (relative) {
        if (negative) v = -v;
        // caller will add currentPos; return delta
        return v;
    }
    return v;
}

// Volume parsed result matches Command VolumeSet fields but header standalone
struct ParsedVolume {
    std::optional<float> level{};
    std::optional<bool> mute{};
    std::optional<int> deltaPct{};
};

inline std::expected<ParsedVolume, std::string> parseVolume(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    if (s.empty()) {
        // no arg -> show, return empty
        return ParsedVolume{};
    }
    if (s == "mute") return ParsedVolume{std::nullopt, true, std::nullopt};
    if (s == "unmute") return ParsedVolume{std::nullopt, false, std::nullopt};
    if (s.front() == '+' || s.front() == '-') {
        bool neg = s.front() == '-';
        std::string_view num = s.substr(1);
        if (num.empty()) return std::unexpected<std::string>{"invalid delta"};
        int iv = 0;
        auto res = std::from_chars(num.data(), num.data() + num.size(), iv);
        if (res.ec != std::errc{} || res.ptr != num.data() + num.size()) {
            return std::unexpected<std::string>{"invalid delta"};
        }
        if (neg) iv = -iv;
        return ParsedVolume{std::nullopt, std::nullopt, iv};
    }
    // absolute 0-100
    {
        int iv = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), iv);
        if (res.ec != std::errc{} || res.ptr != s.data() + s.size()) {
            return std::unexpected<std::string>{"invalid volume"};
        }
        if (iv < 0 || iv > 100) return std::unexpected<std::string>{"volume out of range"};
        return ParsedVolume{static_cast<float>(iv), std::nullopt, std::nullopt};
    }
}

inline std::chrono::duration<double> durationFromSeconds(double s) {
    return std::chrono::duration<double>{s};
}

} // namespace caudio::app::parse
