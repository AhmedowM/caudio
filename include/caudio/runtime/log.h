#ifndef CAUDIO_RUNTIME_LOG_H
#define CAUDIO_RUNTIME_LOG_H

/**
 * @file caudio/runtime/log.h
 * @brief Logger with level filtering and pluggable sinks.
 *
 * A logger owns a minimum level and a sink. The sink is copied into the
 * logger, so a stack-allocated ca_log_sink is fine. If the sink has a
 * destroy callback, the logger takes ownership of the sink context and calls
 * destroy exactly once when the sink is replaced or the logger is destroyed.
 * Replacing a sink is safe while other threads log: a callback that is
 * already running keeps the old context alive until it returns.
 *
 * Sink callbacks are invoked outside the logger lock. They may call back into
 * the logger (for example to read the level) without deadlocking, but the
 * logger itself must not be destroyed while another thread is logging.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log severity. Values are ordered from most to least verbose.
 *
 * CA_LOG_OFF disables all output, including CA_LOG_ERROR.
 */
typedef enum ca_log_level {
    CA_LOG_TRACE = 0,
    CA_LOG_DEBUG = 1,
    CA_LOG_INFO = 2,
    CA_LOG_WARN = 3,
    CA_LOG_ERROR = 4,
    CA_LOG_OFF = 5
} ca_log_level;

/**
 * @brief Return the short upper-case name of @p level ("TRACE", "INFO", ...).
 *
 * Returns "UNKNOWN" for values outside the enum range.
 */
CAUDIO_API const char* ca_log_level_name(ca_log_level level);

/**
 * @brief Opaque logger handle.
 */
typedef struct ca_logger ca_logger;

/**
 * @brief Sink callback. Receives a complete, NUL-terminated message.
 *
 * @param ctx     Sink context supplied in ca_log_sink.
 * @param level   Severity of this record.
 * @param message Message text; never nullptr. May contain embedded newlines.
 * @param len     Message length in bytes, excluding the terminating NUL.
 */
typedef void (*ca_log_sink_write_fn)(void* ctx, ca_log_level level, const char* message,
                                     size_t len);

/**
 * @brief Sink teardown callback, called once when the logger releases the
 *        sink. Must not log through the same logger.
 */
typedef void (*ca_log_sink_destroy_fn)(void* ctx);

/**
 * @brief A log sink: write callback, context, and optional destructor.
 *
 * write must be non-nullptr. ctx may be nullptr. destroy may be nullptr.
 */
typedef struct ca_log_sink {
    ca_log_sink_write_fn write;
    void* ctx;
    ca_log_sink_destroy_fn destroy;
} ca_log_sink;

/**
 * @brief Logger creation parameters.
 */
typedef struct ca_log_config {
    ca_log_level level; /**< Minimum level to emit. */
    ca_log_sink sink;   /**< Copied into the logger. */
} ca_log_config;

/**
 * @brief Initialize @p config to defaults: CA_LOG_INFO and no sink.
 */
CAUDIO_API void ca_log_config_init(ca_log_config* config);

/**
 * @brief Create a logger.
 *
 * @param out    Receives the new logger.
 * @param config Optional configuration; nullptr means CA_LOG_INFO with no sink.
 * @param err    Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_logger_create(ca_logger** out, const ca_log_config* config, ca_error* err);

/**
 * @brief Destroy a logger, releasing its sink. nullptr and *logger == nullptr are
 *        no-ops.
 */
CAUDIO_API void ca_logger_destroy(ca_logger** logger);

/**
 * @brief Set the minimum level. Invalid values are ignored.
 */
CAUDIO_API void ca_logger_set_level(ca_logger* logger, ca_log_level level);

/**
 * @brief Return the minimum level, or CA_LOG_OFF for a nullptr logger.
 */
CAUDIO_API ca_log_level ca_logger_get_level(const ca_logger* logger);

/**
 * @brief Replace the sink. The previous sink is destroyed after the logger
 *        lock is released, so its destructor may call back into the logger.
 *
 * Passing nullptr clears the sink.
 *
 * @return CA_OK on success, CA_INVALID_ARGUMENT for a nullptr logger or a sink
 *         with no write callback.
 */
CAUDIO_API ca_status ca_logger_set_sink(ca_logger* logger, const ca_log_sink* sink, ca_error* err);

/**
 * @brief Emit an already-formatted message.
 *
 * A nullptr logger or message is a no-op. Messages at a level below the
 * configured minimum are dropped.
 */
CAUDIO_API void ca_logger_write(ca_logger* logger, ca_log_level level, const char* message,
                                size_t len);

/**
 * @brief Emit a printf-style message.
 */
CAUDIO_API void ca_logger_vlog(ca_logger* logger, ca_log_level level, const char* fmt,
                               va_list args);

/**
 * @brief Emit a printf-style message.
 */
CAUDIO_API void ca_logger_log(ca_logger* logger, ca_log_level level, const char* fmt, ...)
    CA_PRINTF(3, 4);

/**
 * @brief Opaque file sink.
 */
typedef struct ca_log_file ca_log_file;

/**
 * @brief Open a log file sink.
 *
 * The file is opened for append, so records survive restarts. Every record is
 * flushed before the sink write returns.
 *
 * @param out       Receives the file sink.
 * @param path      UTF-8 path.
 * @param max_bytes Rotate after this many bytes; 0 disables rotation.
 * @param max_files Number of archives to keep (path.1 .. path.N) when
 *                  rotation is enabled. 0 truncates the current file when it
 *                  is full; the maximum accepted value is 1000.
 * @param err       Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_log_file_open(ca_log_file** out, const char* path, size_t max_bytes,
                                      unsigned max_files, ca_error* err);

/**
 * @brief Close a file sink that has not been handed to a logger.
 *
 * After ca_log_file_sink() is given to a logger, the logger owns the sink.
 * Calling ca_log_file_close() on the same handle then is a use-after-free.
 */
CAUDIO_API void ca_log_file_close(ca_log_file** file);

/**
 * @brief Return a sink for @p file.
 *
 * Ownership of @p file transfers to the logger that receives this sink. The
 * sink context is @p file itself; the destroy callback closes it.
 *
 * The file sink is internally synchronized and may be used by several logger
 * callers concurrently.
 */
CAUDIO_API ca_log_sink ca_log_file_sink(ca_log_file* file);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_LOG_H */
