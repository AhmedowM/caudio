#pragma once

/**
 * @file print.hpp
 * @brief Portable `print`/`println` facade over `<format>`.
 * @ingroup caudio_utils
 * @details Routes through `std::format` + classic insertion on every
 * toolchain. MinGW GCC 16.x is included: its `<print>` stream/`FILE*`
 * overloads lower to missing Windows-console helpers, so the format-based
 * path is used universally for byte-identical output. Always call
 * `caudio::print`/`caudio::println`, never `std::` directly.
 */

#include <cstdio>
#include <format>
#include <iostream>
#include <ostream>
#include <string>
#include <utility>

namespace caudio {

/// @brief Format to a stream.
template <typename... A>
void print(std::ostream& os, std::format_string<A...> fmt, A&&... args) {
    os << std::format(fmt, std::forward<A>(args)...);
}

/// @brief Format a line to a stream.
template <typename... A>
void println(std::ostream& os, std::format_string<A...> fmt, A&&... args) {
    os << std::format(fmt, std::forward<A>(args)...) << '\n';
}

/// @brief Format to a C stream.
template <typename... A>
void print(std::FILE* f, std::format_string<A...> fmt, A&&... args) {
    std::fputs(std::format(fmt, std::forward<A>(args)...).c_str(), f);
}

/// @brief Format a line to a C stream.
template <typename... A>
void println(std::FILE* f, std::format_string<A...> fmt, A&&... args) {
    std::string s = std::format(fmt, std::forward<A>(args)...);
    s.push_back('\n');
    std::fputs(s.c_str(), f);
}

/// @brief Format to stdout.
template <typename... A>
void print(std::format_string<A...> fmt, A&&... args) {
    std::cout << std::format(fmt, std::forward<A>(args)...);
}

/// @brief Format a line to stdout.
template <typename... A>
void println(std::format_string<A...> fmt, A&&... args) {
    std::cout << std::format(fmt, std::forward<A>(args)...) << '\n';
}

} // namespace caudio
