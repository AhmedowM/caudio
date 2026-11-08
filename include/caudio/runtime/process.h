#ifndef CAUDIO_RUNTIME_PROCESS_H
#define CAUDIO_RUNTIME_PROCESS_H

/**
 * @file caudio/runtime/process.h
 * @brief Process creation and management.
 *
 * POSIX backend: fork/exec/waitpid. Win32 backend: CreateProcess/WaitForSingleObject.
 * All functions return ca_status and take an optional ca_error *err.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque process handle.
 */
typedef struct ca_process *ca_process_t;

/**
 * @brief Process exit status.
 */
typedef struct ca_process_status {
    int exited;   /* nonzero if the process exited normally */
    int code;     /* exit code if exited, signal number if terminated */
} ca_process_status;

/**
 * @brief Spawn a new process.
 *
 * @param out   Receives the new process handle.
 * @param argv  nullptr-terminated array of argument strings (argv[0] is the
 *              program name). The strings must remain valid until the process
 *              is created.
 * @param env   nullptr-terminated array of "KEY=VALUE" strings, or nullptr to
 *              inherit the caller's environment.
 * @param err   Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_process_spawn(ca_process_t *out, char *const argv[],
                                      char *const env[], ca_error *err);

/**
 * @brief Wait for @p process to exit and collect its status.
 *
 * Blocks until the process terminates. The handle becomes invalid after this
 * call and must not be used again.
 *
 * @param process Process handle.
 * @param status  Receives the exit status, or nullptr if not wanted.
 * @param err     Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_process_wait(ca_process_t process,
                                     ca_process_status *status,
                                     ca_error *err);

/**
 * @brief Terminate @p process immediately.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_process_kill(ca_process_t process, ca_error *err);

/**
 * @brief Return the process ID of the calling process.
 */
CAUDIO_API int ca_process_self(void);

/**
 * @brief Release a process handle without waiting.
 *
 * Use this when you no longer need to track the process but do not want to
 * block on ca_process_wait. The process continues running (orphaned).
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_process_release(ca_process_t process, ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_PROCESS_H */
