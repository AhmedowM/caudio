#include <caudio/db/fts.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace caudio::db::internal {

std::string escapeLike(std::string_view term) {
    std::string result;
    result.reserve(term.size() + 4);
    for (char c : term) {
        if (c == '%' || c == '_' || c == '\\') {
            result.push_back('\\');
        }
        result.push_back(c);
    }
    return result;
}

std::string toHex(const std::array<uint8_t, 32>& fp) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (uint8_t b : fp) {
        s.push_back(hex[b >> 4]);
        s.push_back(hex[b & 0xf]);
    }
    return s;
}

bool fromHex(std::string_view hexStr, std::array<uint8_t, 32>& out) {
    if (hexStr.size() != 64)
        return false;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < 32; i++) {
        int hi = hv(hexStr[i * 2]);
        int lo = hv(hexStr[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

std::string sanitizeFtsTerm(std::string_view term) {
    if (term.empty())
        return {};

    std::string cleaned;
    cleaned.reserve(term.size());
    for (char c : term) {
        if (c == '\'' || c == '*' || c == ':' || c == '-' || c == '(' || c == ')' || c == '^' ||
            c == '~')
            continue;
        if (c == '"') {
            cleaned.push_back('"');
            cleaned.push_back('"');
        } else {
            cleaned.push_back(c);
        }
    }

    std::string upper = cleaned;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    std::string opsWithSpaces[] = {" AND ", " OR ", " NOT ", " NEAR "};
    for (const auto& op : opsWithSpaces) {
        size_t pos = 0;
        while ((pos = upper.find(op, pos)) != std::string::npos) {
            upper.replace(pos, op.size(), std::string(op.size(), ' '));
            cleaned.replace(pos, op.size(), std::string(op.size(), ' '));
            pos += op.size();
        }
    }

    std::string opsStart[] = {"AND ", "OR ", "NOT ", "NEAR "};
    for (const auto& op : opsStart) {
        if (upper.rfind(op, 0) == 0) {
            upper.replace(0, op.size(), std::string(op.size(), ' '));
            cleaned.replace(0, op.size(), std::string(op.size(), ' '));
        }
    }

    std::string opsEnd[] = {" AND", " OR", " NOT", " NEAR"};
    for (const auto& op : opsEnd) {
        if (upper.size() >= op.size() &&
            upper.compare(upper.size() - op.size(), op.size(), op) == 0) {
            size_t pos = upper.size() - op.size();
            upper.replace(pos, op.size(), std::string(op.size(), ' '));
            cleaned.replace(pos, op.size(), std::string(op.size(), ' '));
        }
    }

    std::string result;
    result.reserve(cleaned.size());
    bool lastWasSpace = false;
    for (char c : cleaned) {
        if (c == ' ') {
            if (!lastWasSpace) {
                result.push_back(' ');
                lastWasSpace = true;
            }
        } else {
            result.push_back(c);
            lastWasSpace = false;
        }
    }

    size_t start = 0;
    while (start < result.size() && result[start] == ' ')
        start++;
    size_t end = result.size();
    while (end > start && result[end - 1] == ' ')
        end--;
    if (start >= end)
        return {};

    std::string final = result.substr(start, end - start);

    if (final.empty())
        return {};

    return final;
}

} // namespace caudio::db::internal
