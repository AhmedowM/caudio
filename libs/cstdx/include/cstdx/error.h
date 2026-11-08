/**
 * @file error.h
 * @brief Defines the error types for representing errors in cstdx.
 */

#ifndef CSTDX_ERROR_H
#define CSTDX_ERROR_H

/**
 * @brief Describes an error returned by a cstdx operation.
 * @details The message is a non-owning pointer. Its storage must remain valid
 * while the error is being inspected.
 */
typedef struct cx_error {
    /** Numeric error code defined by the API using the error. */
    int code;

    /** Human-readable error message, or nullptr when no message is available. */
    const char* message;
} cx_error;

#endif // CSTDX_ERROR_H
