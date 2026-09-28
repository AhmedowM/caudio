// Umbrella mirrors include/caudio.hpp (all libraries).
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
export import caudio.ipc;
export import caudio.service;
export import caudio.client;
