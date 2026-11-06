#ifndef CAUDIO_RUNTIME_COND_H
#define CAUDIO_RUNTIME_COND_H

/**
 * @file caudio/runtime/cond.h
 * @brief Condition variable.
 *
 * Used with ca_mutex_t: a thread waits on the condition while holding the
 * mutex; another thread signals or broadcasts to wake waiters.
 * POSIX: pthread_cond_t. Win32: CONDITION_VARIABLE.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/mutex.h>
#include <caudio/runtime/time.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque condition variable handle.
 */
typedef struct ca_cond *ca_cond_t;

/**
 * @brief Create a condition variable.
 *
 * @param out Receives the new condition handle.
 * @param err Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_cond_create(ca_cond_t *out, ca_error *err);

/**
 * @brief Destroy a condition variable. No threads may be waiting on it.
 *
 * Passing NULL is a no-op.
 */
CAUDIO_API void ca_cond_destroy(ca_cond_t cond);

/**
 * @brief Wake one waiting thread.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_cond_signal(ca_cond_t cond, ca_error *err);

/**
 * @brief Wake all waiting threads.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_cond_broadcast(ca_cond_t cond, ca_error *err);

/**
 * @brief Wait on @p cond, atomically releasing @p mutex and re-acquiring it
 *        before returning. @p mutex must be held by the calling thread.
 *
 * @return CA_OK on success (spurious wakeups possible).
 */
CAUDIO_API ca_status ca_cond_wait(ca_cond_t cond, ca_mutex_t mutex,
                                  ca_error *err);

/**
 * @brief Wait on @p cond with an absolute timeout (microseconds since
 *        monotonic epoch).
 *
 * @return CA_OK if woken, CA_TIMED_OUT if the timeout elapsed.
 */
CAUDIO_API ca_status ca_cond_timedwait(ca_cond_t cond, ca_mutex_t mutex,
                                       ca_time_us_t abstime_us,
                                       ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_COND_H */
