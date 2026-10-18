#pragma once

/**
 * @file pch.hpp
 * @brief Precompiled-header preamble for caudio components.
 * @details Stable, widely-used standard-library headers that change rarely.
 * Do NOT add project headers you edit frequently here -- every edit forces
 * every consumer TU to rebuild. Keep this set small.
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
