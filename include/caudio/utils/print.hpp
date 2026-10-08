#pragma once

/**
 * @file print.hpp
 * @brief Portable `print`/`println` facade over `<print>` / `<format>`.
 * @ingroup caudio_utils
 * @details libstdc++ on MinGW GCC 16.x (notably the msys2 UCRT build) ships
 * `<print>` headers whose stream/`FILE*` overloads lower to the
 * Windows-console helpers `std::__open_terminal` / `std::__write_to_terminal`,
 * which are absent from its `libstdc++` binary (`libstdc++exp` no longer
 * exists post-merge) -- any TU using `std::println(cout/cerr/os)` or bare
 * `std::println(...)` fails at link with undefined references. Other
 * toolchains are unaffected.
 *
 * This header routes through `std::format` + classic insertion on MinGW
 * (byte-identical output for our UTF-8/ASCII usage; no console transcoding
 * is performed, matching the daemon/pipe use case) and forwards to
 * `std::print`/`std::println` everywhere else. Always call
 * `caudio::print`/`caudio::println`, never `std::` directly.
 */

#include <cstdio>
#include <format>
#include <ostream>
#include <string>
#include <utility>

#if defined(__MINGW32__)
#include <iostream>
#else
#include <print>
#endif

namespace caudio {

#ifdef __MINGW32__

/// @brief Format to a stream (MinGW: avoids missing terminal helpers).
template <typename... A>
void print(std::ostream& os, std::format_string<A...> fmt, A&&... args) {
    os << std::format(fmt, std::forward<A>(args)...);
}

/// @brief Format a line to a stream (MinGW).
template <typename... A>
void println(std::ostream& os, std::format_string<A...> fmt, A&&... args) {
    os << std::format(fmt, std::forward<A>(args)...) << '\n';
}

/// @brief Format to a C stream (MinGW).
template <typename... A>
void print(std::FILE* f, std::format_string<A...> fmt, A&&... args) {
    std::fputs(std::format(fmt, std::forward<A>(args)...).c_str(), f);
}

/// @brief Format a line to a C stream (MinGW).
template <typename... A>
void println(std::FILE* f, std::format_string<A...> fmt, A&&... args) {
    std::string s = std::format(fmt, std::forward<A>(args)...);
    s.push_back('\n');
    std::fputs(s.c_str(), f);
}

/// @brief Format to stdout (MinGW).
template <typename... A>
void print(std::format_string<A...> fmt, A&&... args) {
    std::cout << std::format(fmt, std::forward<A>(args)...);
}

/// @brief Format a line to stdout (MinGW).
template <typename... A>
void println(std::format_string<A...> fmt, A&&... args) {
    std::cout << std::format(fmt, std::forward<A>(args)...) << '\n';
}

#else

using std::print;
using std::println;

#endif

} // namespace caudio
