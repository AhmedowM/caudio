#pragma once

/**
 * @file pch.hpp
 * @brief Precompiled-header preamble for caudio components.
 * @details Stable, widely-used standard-library headers that change rarely.
 * Do NOT add project headers you edit frequently here -- every edit forces
 * every consumer TU to rebuild. Keep this set small.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <chrono>
#include <thread>
#include <filesystem>
#include <format>
