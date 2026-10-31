#pragma once

/**
 * @file utils.hpp
 * @brief Main header for caudio.utils -- includes all utility partitions.
 * @defgroup caudio_utils caudio utilities
 * @details This header includes all public partitions of caudio.utils:
 * - result: StatusCode, toString, formatter
 * - error: Error, Expected, makeError
 * - log: LogLevel, Logger
 * - math: clampVolume, toHex, fromHex
 * - ring: SpscRing
 * - thread: sleepFor, sleepForMs, setThreadName
 * - print: portable print/println facade (see print.hpp)
 * - json: opaque Json/JsonRef value type (nlohmann stays behind src/ walls)
 * - record_json: Track <-> Json codec shared by db and ipc
 * - generator: portable Generator (libc++ lacks std::generator)
 *
 * Internal-only helpers (MpscQueue, MoveOnlyFunction, the rotating file
 * sink) live in src/utils/ and are NOT installed.
 */

#include <caudio/utils/error.hpp>
#include <caudio/utils/generator.hpp>
#include <caudio/utils/json.hpp>
#include <caudio/utils/log.hpp>
#include <caudio/utils/math.hpp>
#include <caudio/utils/print.hpp>
#include <caudio/utils/record_json.hpp>
#include <caudio/utils/result.hpp>
#include <caudio/utils/ring.hpp>
#include <caudio/utils/thread.hpp>
