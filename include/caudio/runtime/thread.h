#ifndef CAUDIO_RUNTIME_THREAD_H
#define CAUDIO_RUNTIME_THREAD_H

/**
 * @file caudio/runtime/thread.h
 * @brief Thread creation and joining.
 *
 * POSIX backend: pthread. Win32 backend: CreateThread / WaitForSingleObject.
 * All functions return ca_status and take an optional ca_error *err.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque thread handle.
 */
typedef struct ca_thread* ca_thread_t;

/**
 * @brief Thread entry point signature.
 *
 * @param arg Opaque argument passed from ca_thread_create.
 * @return Opaque return value, retrieved with ca_thread_join.
 */
typedef void* (*ca_thread_fn)(void* arg);

/**
 * @brief Create and start a thread.
 *
 * The thread begins running @p fn with @p arg immediately. Use ca_thread_join
 * to wait for completion and collect the return value.
 *
 * @param out Receives the new thread handle.
 * @param fn  Thread entry point.
 * @param arg Argument passed to @p fn.
 * @param err Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_thread_create(ca_thread_t* out, ca_thread_fn fn, void* arg, ca_error* err);

/**
 * @brief Wait for @p thread to finish and collect its return value.
 *
 * @param thread   Thread handle (must be joinable).
 * @param out_ret  Receives the thread's return value, or nullptr if not wanted.
 * @param err      Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_thread_join(ca_thread_t thread, void** out_ret, ca_error* err);

/**
 * @brief Detach @p thread so its resources are reclaimed automatically on
 *        exit; the handle can no longer be joined.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_thread_detach(ca_thread_t thread, ca_error* err);

/**
 * @brief Yield the CPU (hint to the scheduler).
 */
CAUDIO_API void ca_thread_yield(void);

/**
 * @brief Return the numeric identifier of the calling thread.
 */
CAUDIO_API uint64_t ca_thread_self(void);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_THREAD_H */
