#ifndef CAUDIO_RUNTIME_TIME_H
#define CAUDIO_RUNTIME_TIME_H

/**
 * @file caudio/runtime/time.h
 * @brief Monotonic clock and time utilities.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Monotonic timestamp in microseconds. Not related to wall-clock time;
 *        suitable for measuring intervals.
 */
typedef uint64_t ca_time_us_t;

/**
 * @brief Read the monotonic clock into @p out (microseconds).
 *
 * @return CA_OK on success, or an error status with @p err set.
 */
CAUDIO_API ca_status ca_time_now(ca_time_us_t *out, ca_error *err);

/**
 * @brief Sleep for at least @p microseconds. May sleep longer.
 *
 * @return CA_OK on success, or an error status with @p err set.
 */
CAUDIO_API ca_status ca_time_sleep_us(ca_time_us_t microseconds,
                                      ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_TIME_H */
