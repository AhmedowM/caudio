#pragma once
/**
 * @file utils.hpp
 * @brief Main header for caudio.utils -- includes all utility partitions.
 * @defgroup caudio_utils caudio utilities
 * @ingroup caudio_utils
 * @details This header includes all public partitions of caudio.utils:
 * - result: StatusCode, toString, formatter
 * - error: Error, Expected, makeError
 * - log: LogLevel, Logger
 * - math: clampVolume, toHex, fromHex
 * - ring: SpscRing
 * - mpsc_queue: MpscQueue
 * - thread: sleepFor, sleepForMs, setThreadName
 * - print: portable print/println facade (see print.hpp)
 * - json: opaque Json/JsonRef value type (nlohmann stays behind src/ walls)
 * - function: portable MoveOnlyFunction (libc++ lacks std::move_only_function)
 * - generator: portable Generator (libc++ lacks std::generator)
 */

#include <caudio/utils/error.hpp>
#include <caudio/utils/function.hpp>
#include <caudio/utils/generator.hpp>
#include <caudio/utils/json.hpp>
#include <caudio/utils/log.hpp>
#include <caudio/utils/math.hpp>
#include <caudio/utils/mpsc_queue.hpp>
#include <caudio/utils/print.hpp>
#include <caudio/utils/result.hpp>
#include <caudio/utils/ring.hpp>
#include <caudio/utils/thread.hpp>

