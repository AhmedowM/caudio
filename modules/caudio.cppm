// Umbrella mirrors include/caudio.hpp (utils/player/db/engine); IPC lives in caudio.ipc.
module;
#include <string_view>

/**
 * @file caudio.cppm
 * @brief Umbrella module for caudio.
 * @ingroup caudio
 * @details Thin mirror of `include/caudio.hpp`, which holds the canonical
 * layer, threading and error-handling documentation. See also `caudio.ipc`,
 * `caudio.service` and `caudio.client`.
 */
export module caudio;

export import caudio.utils;
export import caudio.player;
export import caudio.db;
export import caudio.engine;
