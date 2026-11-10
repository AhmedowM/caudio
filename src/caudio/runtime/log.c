#include <caudio/runtime/log.h>
#include <caudio/runtime/mutex.h>
#include <caudio/runtime/thread.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <cstdx/allocator.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/**
 * @file caudio/runtime/log.c
 * @brief Logger core and the file sink.
 *
 * The logger keeps the hot-path level check atomic and copies the sink under
 * a mutex, then invokes it after unlocking. This keeps sink callbacks free to
 * call back into the logger without deadlocking. The file sink has its own
 * mutex because logger callbacks are intentionally serialized only within the
 * logger's critical section, not across the callback itself.
 */

struct ca_logger;

/**
 * @brief Reference-counted sink.
 *
 * A write acquires a reference under the logger mutex before releasing the
 * lock, so replacing or clearing a sink cannot free the context while a
 * callback is still running. Re-entrant sinks work as well: the outer write
 * keeps the old reference alive until it returns.
 */
typedef struct log_sink_ref {
    _Atomic unsigned refs;
    ca_log_sink sink;
} log_sink_ref;

struct ca_logger {
    ca_mutex_t mutex;
    _Atomic int level;
    log_sink_ref* sink; /* guarded by mutex */
};

static log_sink_ref* log_sink_ref_new(ca_log_sink sink) {
    const cx_allocator* alloc = cx_allocator_default();
    log_sink_ref* ref = (log_sink_ref*)cx_allocator_alloc(alloc, sizeof(*ref), alignof(*ref));
    if (ref == nullptr) {
        return nullptr;
    }

    atomic_init(&ref->refs, 1);
    ref->sink = sink;
    return ref;
}

static void log_sink_ref_acquire(log_sink_ref* ref) {
    (void)atomic_fetch_add_explicit(&ref->refs, 1, memory_order_relaxed);
}

static void log_sink_ref_release(log_sink_ref* ref) {
    if (ref == nullptr) {
        return;
    }

    if (atomic_fetch_sub_explicit(&ref->refs, 1, memory_order_acq_rel) == 1) {
        if (ref->sink.destroy) {
            ref->sink.destroy(ref->sink.ctx);
        }
        const cx_allocator* alloc = cx_allocator_default();
        cx_allocator_free(alloc, ref, sizeof(*ref), alignof(*ref));
    }
}

/* Shortest-path check used before formatting and again inside write. */
static bool log_level_valid(ca_log_level level) {
    return (int)level >= (int)CA_LOG_TRACE && (int)level <= (int)CA_LOG_OFF;
}

static bool log_level_enabled(const ca_logger* logger, ca_log_level level) {
    if (logger == nullptr || !log_level_valid(level) || level == CA_LOG_OFF) {
        return false;
    }

    ca_log_level minimum = (ca_log_level)atomic_load_explicit(&logger->level, memory_order_relaxed);
    return minimum != CA_LOG_OFF && level >= minimum;
}

const char* ca_log_level_name(ca_log_level level) {
    switch (level) {
    case CA_LOG_TRACE:
        return "TRACE";
    case CA_LOG_DEBUG:
        return "DEBUG";
    case CA_LOG_INFO:
        return "INFO";
    case CA_LOG_WARN:
        return "WARN";
    case CA_LOG_ERROR:
        return "ERROR";
    case CA_LOG_OFF:
        return "OFF";
    default:
        return "UNKNOWN";
    }
}

void ca_log_config_init(ca_log_config* config) {
    if (config == nullptr) {
        return;
    }

    config->level = CA_LOG_INFO;
    config->sink = (ca_log_sink){0};
}

ca_status ca_logger_create(ca_logger** out, const ca_log_config* config, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = nullptr;

    ca_log_level level = CA_LOG_INFO;
    ca_log_sink sink = {0};

    if (config) {
        if (!log_level_valid(config->level)) {
            if (err) {
                ca_error_set(err, CA_INVALID_ARGUMENT, "invalid log level: %d", (int)config->level);
            }
            return CA_INVALID_ARGUMENT;
        }

        level = config->level;
        sink = config->sink;

        if (!sink.write && (sink.ctx != nullptr || sink.destroy != nullptr)) {
            if (err) {
                ca_error_set(err, CA_INVALID_ARGUMENT, "sink has context or destroy without write");
            }
            return CA_INVALID_ARGUMENT;
        }
    }

    const cx_allocator* alloc = cx_allocator_default();
    ca_logger* logger = (ca_logger*)cx_allocator_alloc(alloc, sizeof(*logger), alignof(*logger));
    if (logger == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate logger");
        }
        return CA_OUT_OF_MEMORY;
    }
    logger->sink = nullptr;

    ca_status status = ca_mutex_create(&logger->mutex, err);
    if (status != CA_OK) {
        cx_allocator_free(alloc, logger, sizeof(*logger), alignof(*logger));
        return status;
    }

    if (sink.write) {
        logger->sink = log_sink_ref_new(sink);
        if (logger->sink == nullptr) {
            ca_mutex_destroy(logger->mutex);
            cx_allocator_free(alloc, logger, sizeof(*logger), alignof(*logger));
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate log sink");
            }
            return CA_OUT_OF_MEMORY;
        }
    }

    atomic_init(&logger->level, (int)level);

    *out = logger;
    return CA_OK;
}

void ca_logger_destroy(ca_logger** logger) {
    if (logger == nullptr || *logger == nullptr) {
        return;
    }

    ca_logger* l = *logger;
    *logger = nullptr;

    (void)ca_mutex_lock(l->mutex, nullptr);
    log_sink_ref* sink = l->sink;
    l->sink = nullptr;
    (void)ca_mutex_unlock(l->mutex, nullptr);

    ca_mutex_destroy(l->mutex);
    log_sink_ref_release(sink);

    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, l, sizeof(*l), alignof(*l));
}

void ca_logger_set_level(ca_logger* logger, ca_log_level level) {
    if (logger == nullptr || !log_level_valid(level)) {
        return;
    }

    atomic_store_explicit(&logger->level, (int)level, memory_order_relaxed);
}

ca_log_level ca_logger_get_level(const ca_logger* logger) {
    if (logger == nullptr) {
        return CA_LOG_OFF;
    }

    return (ca_log_level)atomic_load_explicit(&logger->level, memory_order_relaxed);
}

ca_status ca_logger_set_sink(ca_logger* logger, const ca_log_sink* sink, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (logger == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "logger is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    log_sink_ref* next = nullptr;
    if (sink) {
        if (!sink->write) {
            if (err) {
                ca_error_set(err, CA_INVALID_ARGUMENT, "sink.write is nullptr");
            }
            return CA_INVALID_ARGUMENT;
        }

        next = log_sink_ref_new(*sink);
        if (next == nullptr) {
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate log sink");
            }
            return CA_OUT_OF_MEMORY;
        }
    }

    ca_status status = ca_mutex_lock(logger->mutex, err);
    if (status != CA_OK) {
        /* Ownership was not transferred; the caller still owns the sink. */
        const cx_allocator* alloc = cx_allocator_default();
        cx_allocator_free(alloc, next, sizeof(*next), alignof(*next));
        return status;
    }

    log_sink_ref* old = logger->sink;
    logger->sink = next;

    status = ca_mutex_unlock(logger->mutex, err);

    /* Drop the logger's reference after unlocking. An in-flight callback holds
     * its own reference, so the old sink context stays alive until it
     * returns; the destructor is then free to call back into the logger. */
    log_sink_ref_release(old);

    return status;
}

void ca_logger_write(ca_logger* logger, ca_log_level level, const char* message, size_t len) {
    if (message == nullptr || !log_level_enabled(logger, level)) {
        return;
    }

    if (ca_mutex_lock(logger->mutex, nullptr) != CA_OK) {
        return;
    }

    ca_log_level minimum = (ca_log_level)atomic_load_explicit(&logger->level, memory_order_relaxed);
    log_sink_ref* sink = logger->sink;
    if (sink) {
        log_sink_ref_acquire(sink);
    }

    (void)ca_mutex_unlock(logger->mutex, nullptr);

    if (minimum == CA_LOG_OFF || level < minimum || !sink) {
        log_sink_ref_release(sink);
        return;
    }

    sink->sink.write(sink->sink.ctx, level, message, len);
    log_sink_ref_release(sink);
}

void ca_logger_vlog(ca_logger* logger, ca_log_level level, const char* fmt, va_list args) {
    if (fmt == nullptr || !log_level_enabled(logger, level)) {
        return;
    }

    char stack_buffer[1024];

    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(stack_buffer, sizeof(stack_buffer), fmt, copy);
    va_end(copy);

    if (needed < 0) {
        return;
    }

    if ((size_t)needed < sizeof(stack_buffer)) {
        ca_logger_write(logger, level, stack_buffer, (size_t)needed);
        return;
    }

    const cx_allocator* alloc = cx_allocator_default();
    char* heap_buffer = (char*)cx_allocator_alloc(alloc, (size_t)needed + 1, 1);
    if (heap_buffer == nullptr) {
        /* Allocation failure must not lose the record entirely. The stack
         * buffer still holds a truncated, NUL-terminated version. */
        ca_logger_write(logger, level, stack_buffer, sizeof(stack_buffer) - 1);
        return;
    }

    va_copy(copy, args);
    (void)vsnprintf(heap_buffer, (size_t)needed + 1, fmt, copy);
    va_end(copy);

    ca_logger_write(logger, level, heap_buffer, (size_t)needed);
    cx_allocator_free(alloc, heap_buffer, (size_t)needed + 1, 1);
}

void ca_logger_log(ca_logger* logger, ca_log_level level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ca_logger_vlog(logger, level, fmt, args);
    va_end(args);
}

/* ------------------------------------------------------------------------- */
/* File sink                                                                 */
/* ------------------------------------------------------------------------- */

struct ca_log_file {
    ca_mutex_t mutex;
    FILE* fp;
    char* path;
    size_t max_bytes;
    unsigned max_files;
    uint64_t size;
};

#if defined(_WIN32)
static WCHAR* log_utf8_to_wide(const char* text) {
    const cx_allocator* alloc = cx_allocator_default();
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (len <= 0) {
        return nullptr;
    }

    WCHAR* wide = (WCHAR*)cx_allocator_alloc(alloc, (size_t)len * sizeof(WCHAR), alignof(WCHAR));
    if (wide == nullptr) {
        return nullptr;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, len) != len) {
        cx_allocator_free(alloc, wide, (size_t)len * sizeof(WCHAR), alignof(WCHAR));
        return nullptr;
    }

    return wide;
}

static FILE* log_fopen(const char* path, bool append) {
    WCHAR* wide = log_utf8_to_wide(path);
    if (wide == nullptr) {
        return nullptr;
    }

    FILE* fp = _wfopen(wide, append ? L"ab" : L"wb");
    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, wide, 0, alignof(WCHAR));
    return fp;
}

static void log_remove_file(const char* path) {
    WCHAR* wide = log_utf8_to_wide(path);
    if (wide) {
        (void)_wremove(wide);
        const cx_allocator* alloc = cx_allocator_default();
        cx_allocator_free(alloc, wide, 0, alignof(WCHAR));
    }
}

static bool log_rename_file(const char* from, const char* to) {
    WCHAR* wide_from = log_utf8_to_wide(from);
    WCHAR* wide_to = log_utf8_to_wide(to);
    bool ok = false;

    if (wide_from && wide_to) {
        ok = MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING) != 0;
    }

    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, wide_from, 0, alignof(WCHAR));
    cx_allocator_free(alloc, wide_to, 0, alignof(WCHAR));
    return ok;
}

static int log_seek_end(FILE* fp) { return _fseeki64(fp, 0, SEEK_END); }

static int64_t log_tell(FILE* fp) { return (int64_t)_ftelli64(fp); }
#else
static FILE* log_fopen(const char* path, bool append) { return fopen(path, append ? "ab" : "wb"); }

static void log_remove_file(const char* path) { (void)remove(path); }

static bool log_rename_file(const char* from, const char* to) { return rename(from, to) == 0; }

static int log_seek_end(FILE* fp) { return fseeko(fp, 0, SEEK_END); }

static int64_t log_tell(FILE* fp) { return (int64_t)ftello(fp); }
#endif

static void log_utc_timestamp(char* buf, size_t size) {
#if defined(_WIN32)
    SYSTEMTIME st;
    GetSystemTime(&st);

    (void)snprintf(buf, size, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", (unsigned)st.wYear,
                   (unsigned)st.wMonth, (unsigned)st.wDay, (unsigned)st.wHour,
                   (unsigned)st.wMinute, (unsigned)st.wSecond, (unsigned)st.wMilliseconds);
#else
    struct timespec ts;

    /* clock_gettime() is POSIX and available on Android API 28; timespec_get()
     * is C11 but not exposed by older Bionic headers. */
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        (void)snprintf(buf, size, "0000-00-00T00:00:00.000Z");
        return;
    }

    struct tm utc;
    if (gmtime_r(&ts.tv_sec, &utc) == nullptr) {
        (void)snprintf(buf, size, "0000-00-00T00:00:00.000Z");
        return;
    }

    (void)snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ", utc.tm_year + 1900,
                   utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                   (long)(ts.tv_nsec / 1000000L));
#endif
}

static void log_file_free(ca_log_file* file) {
    if (file == nullptr) {
        return;
    }

    const cx_allocator* alloc = cx_allocator_default();

    if (file->fp) {
        (void)fclose(file->fp);
    }

    ca_mutex_destroy(file->mutex);
    cx_allocator_free(alloc, file->path, file->path ? strlen(file->path) + 1 : 1, 1);
    cx_allocator_free(alloc, file, sizeof(*file), alignof(*file));
}

static bool log_file_open_stream(ca_log_file* file, bool append) {
    if (file->fp) {
        (void)fclose(file->fp);
        file->fp = nullptr;
    }

    file->size = 0;
    file->fp = log_fopen(file->path, append);
    if (file->fp == nullptr) {
        return false;
    }

    if (log_seek_end(file->fp) == 0) {
        int64_t position = log_tell(file->fp);
        if (position > 0) {
            file->size = (uint64_t)position;
        }
    }

    return true;
}

static void log_file_invalidate(ca_log_file* file) {
    if (file->fp) {
        (void)fclose(file->fp);
        file->fp = nullptr;
    }
    file->size = 0;
}

static char* log_rotated_path(const char* path, unsigned index) {
    int needed = snprintf(nullptr, 0, "%s.%u", path, index);
    if (needed < 0) {
        return nullptr;
    }

    const cx_allocator* alloc = cx_allocator_default();
    char* result = (char*)cx_allocator_alloc(alloc, (size_t)needed + 1, 1);
    if (result == nullptr) {
        return nullptr;
    }

    (void)snprintf(result, (size_t)needed + 1, "%s.%u", path, index);
    return result;
}

static void log_rotate_files(ca_log_file* file) {
    if (file->max_files == 0) {
        return;
    }

    char* oldest = log_rotated_path(file->path, file->max_files);
    if (oldest == nullptr) {
        return;
    }
    log_remove_file(oldest);
    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, oldest, (strlen(oldest) + 1), 1);

    for (unsigned i = file->max_files; i > 1; --i) {
        char* from = log_rotated_path(file->path, i - 1);
        char* to = log_rotated_path(file->path, i);
        if (from == nullptr || to == nullptr) {
            cx_allocator_free(alloc, from, from ? strlen(from) + 1 : 1, 1);
            cx_allocator_free(alloc, to, to ? strlen(to) + 1 : 1, 1);
            return;
        }

        (void)log_rename_file(from, to);
        cx_allocator_free(alloc, from, strlen(from) + 1, 1);
        cx_allocator_free(alloc, to, strlen(to) + 1, 1);
    }

    char* first = log_rotated_path(file->path, 1);
    if (first == nullptr) {
        return;
    }
    (void)log_rename_file(file->path, first);
    cx_allocator_free(alloc, first, strlen(first) + 1, 1);
}

static void log_rotate_locked(ca_log_file* file) {
    if (file->fp) {
        (void)fclose(file->fp);
        file->fp = nullptr;
    }
    file->size = 0;

    if (file->max_files > 0) {
        log_rotate_files(file);
        (void)log_file_open_stream(file, true);
    } else {
        (void)log_file_open_stream(file, false);
    }
}

static void ca_log_file_sink_write(void* ctx, ca_log_level level, const char* message, size_t len) {
    ca_log_file* file = (ca_log_file*)ctx;
    if (file == nullptr || message == nullptr) {
        return;
    }

    if (ca_mutex_lock(file->mutex, nullptr) != CA_OK) {
        return;
    }

    if (file->fp == nullptr) {
        (void)ca_mutex_unlock(file->mutex, nullptr);
        return;
    }

    char timestamp[96];
    log_utc_timestamp(timestamp, sizeof(timestamp));

    char header[256];
    int header_len = snprintf(header, sizeof(header), "[%s] [%s] [%" PRIu64 "] ", timestamp,
                              ca_log_level_name(level), ca_thread_self());
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        (void)ca_mutex_unlock(file->mutex, nullptr);
        return;
    }

    size_t total = (size_t)header_len + len + 1;

    if (file->max_bytes > 0 && file->size > 0 &&
        (total > file->max_bytes || file->size > file->max_bytes - total)) {
        log_rotate_locked(file);
        if (file->fp == nullptr) {
            (void)ca_mutex_unlock(file->mutex, nullptr);
            return;
        }
    }

    bool ok = fwrite(header, 1, (size_t)header_len, file->fp) == (size_t)header_len &&
              fwrite(message, 1, len, file->fp) == len && fputc('\n', file->fp) != EOF &&
              fflush(file->fp) == 0;

    if (ok) {
        file->size += total;
    } else {
        log_file_invalidate(file);
    }

    (void)ca_mutex_unlock(file->mutex, nullptr);
}

static void ca_log_file_sink_destroy(void* ctx) { log_file_free((ca_log_file*)ctx); }

ca_status ca_log_file_open(ca_log_file** out, const char* path, size_t max_bytes,
                           unsigned max_files, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = nullptr;

    if (path == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "path is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (max_files > 1000) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "max_files exceeds 1000");
        }
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator* alloc = cx_allocator_default();
    ca_log_file* file = (ca_log_file*)cx_allocator_alloc(alloc, sizeof(*file), alignof(*file));
    if (file == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate log file sink");
        }
        return CA_OUT_OF_MEMORY;
    }

    size_t path_len = strlen(path);
    file->path = (char*)cx_allocator_alloc(alloc, path_len + 1, 1);
    if (file->path == nullptr) {
        cx_allocator_free(alloc, file, sizeof(*file), alignof(*file));
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to copy log path");
        }
        return CA_OUT_OF_MEMORY;
    }
    memcpy(file->path, path, path_len + 1);

    ca_status status = ca_mutex_create(&file->mutex, err);
    if (status != CA_OK) {
        cx_allocator_free(alloc, file->path, path_len + 1, 1);
        cx_allocator_free(alloc, file, sizeof(*file), alignof(*file));
        return status;
    }

    file->max_bytes = max_bytes;
    file->max_files = max_files;
    file->fp = nullptr;
    file->size = 0;

    if (!log_file_open_stream(file, true)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "failed to open log file: %s", path);
        }
        ca_mutex_destroy(file->mutex);
        cx_allocator_free(alloc, file->path, path_len + 1, 1);
        cx_allocator_free(alloc, file, sizeof(*file), alignof(*file));
        return CA_IO_ERROR;
    }

    *out = file;
    return CA_OK;
}

void ca_log_file_close(ca_log_file** file) {
    if (file == nullptr || *file == nullptr) {
        return;
    }

    ca_log_file* f = *file;
    *file = nullptr;
    log_file_free(f);
}

ca_log_sink ca_log_file_sink(ca_log_file* file) {
    ca_log_sink sink = {0};

    if (file) {
        sink.write = ca_log_file_sink_write;
        sink.ctx = file;
        sink.destroy = ca_log_file_sink_destroy;
    }

    return sink;
}
