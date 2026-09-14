/**
 * @file decoder_common.cppm
 * @brief Common decoder utilities and helper functions
 * @ingroup caudio_player
 *
 * This module provides shared utilities for decoder implementations including
 * time formatting, seek time parsing, and other common helper functions.
 * These utilities are used across multiple decoder implementations to ensure
 * consistent behavior.
 *
 * Currently contains the namespace declaration for future utilities.
 * Planned additions:
 * - formatTime(): Format duration as HH:MM:SS or MM:SS
 * - parseSeekTime(): Parse seek strings like "1:30", "90s", "1.5m"
 */

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

export module caudio.player:decoder_common;

namespace caudio::player::detail {} // namespace caudio::player::detail
