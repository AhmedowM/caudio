#include <caudio.h>
#include <caudio/runtime/log.h>
#include <caudio/runtime/thread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ca_test.h"

#define LOG_FILE "caudio_test_log_file.log"
#define ROTATE_FILE "caudio_test_log_rotate.log"

typedef struct capture {
    ca_mutex_t mutex;
    ca_log_level level;
    char text[4096];
    size_t len;
    int count;
} capture;

static void capture_init(capture* cap) {
    memset(cap, 0, sizeof(*cap));
    (void)ca_mutex_create(&cap->mutex, nullptr);
}

static void capture_destroy(capture* cap) { ca_mutex_destroy(cap->mutex); }

static void capture_write(void* ctx, ca_log_level level, const char* message, size_t len) {
    capture* cap = (capture*)ctx;
    (void)ca_mutex_lock(cap->mutex, nullptr);

    cap->level = level;
    cap->count++;

    if (cap->len + len + 1 < sizeof(cap->text)) {
        memcpy(cap->text + cap->len, message, len);
        cap->len += len;
        cap->text[cap->len] = '\0';
    }

    (void)ca_mutex_unlock(cap->mutex, nullptr);
}

static void noop_write(void* ctx, ca_log_level level, const char* message, size_t len) {
    (void)ctx;
    (void)level;
    (void)message;
    (void)len;
}

static void count_destroy(void* ctx) {
    int* counter = (int*)ctx;
    (*counter)++;
}

static int make_capture_logger(ca_logger** out, capture* cap, ca_log_level level) {
    ca_log_config config;
    ca_log_config_init(&config);
    config.level = level;
    config.sink.write = capture_write;
    config.sink.ctx = cap;

    ca_error err;
    ca_error_init(&err);
    ca_status status = ca_logger_create(out, &config, &err);
    if (status != CA_OK) {
        fprintf(stderr, "ca_logger_create: %s\n", err.message);
    }
    return status == CA_OK;
}

static int test_config_init(void) {
    ca_log_config config;
    memset(&config, 0xAA, sizeof(config));

    ca_log_config_init(&config);
    CA_CHECK_EQ(CA_LOG_INFO, config.level);
    CA_CHECK(config.sink.write == nullptr);
    CA_CHECK(config.sink.ctx == nullptr);
    CA_CHECK(config.sink.destroy == nullptr);

    ca_log_config_init(nullptr); /* no-op */

    return ca_test_finish("log.config_init");
}

static int test_level_names(void) {
    CA_CHECK_EQ(0, strcmp(ca_log_level_name(CA_LOG_TRACE), "TRACE"));
    CA_CHECK_EQ(0, strcmp(ca_log_level_name(CA_LOG_DEBUG), "DEBUG"));
    CA_CHECK_EQ(0, strcmp(ca_log_level_name(CA_LOG_INFO), "INFO"));
    CA_CHECK_EQ(0, strcmp(ca_log_level_name(CA_LOG_WARN), "WARN"));
    CA_CHECK_EQ(0, strcmp(ca_log_level_name(CA_LOG_ERROR), "ERROR"));
    CA_CHECK_EQ(0, strcmp(ca_log_level_name(CA_LOG_OFF), "OFF"));
    CA_CHECK_EQ(0, strcmp(ca_log_level_name((ca_log_level)99), "UNKNOWN"));

    return ca_test_finish("log.level_names");
}

static int test_levels_and_callback(void) {
    capture cap;
    capture_init(&cap);

    ca_logger* logger = nullptr;
    CA_CHECK(make_capture_logger(&logger, &cap, CA_LOG_INFO));

    ca_logger_log(logger, CA_LOG_TRACE, "trace %d", 1);
    ca_logger_log(logger, CA_LOG_DEBUG, "debug");
    ca_logger_log(logger, CA_LOG_INFO, "info %d", 2);
    ca_logger_log(logger, CA_LOG_WARN, "warn");
    ca_logger_log(logger, CA_LOG_ERROR, "error");

    CA_CHECK_EQ(3, cap.count);
    CA_CHECK(strstr(cap.text, "info 2") != nullptr);
    CA_CHECK(strstr(cap.text, "warn") != nullptr);
    CA_CHECK(strstr(cap.text, "error") != nullptr);
    CA_CHECK(strstr(cap.text, "trace") == nullptr);
    CA_CHECK(strstr(cap.text, "debug") == nullptr);
    CA_CHECK_EQ(CA_LOG_ERROR, cap.level);

    ca_logger_set_level(logger, CA_LOG_ERROR);
    CA_CHECK_EQ(CA_LOG_ERROR, ca_logger_get_level(logger));
    ca_logger_log(logger, CA_LOG_WARN, "dropped");
    CA_CHECK_EQ(3, cap.count);

    ca_logger_set_level(logger, CA_LOG_TRACE);
    ca_logger_log(logger, CA_LOG_TRACE, "trace again");
    CA_CHECK_EQ(4, cap.count);

    ca_logger_set_level(logger, CA_LOG_OFF);
    ca_logger_log(logger, CA_LOG_ERROR, "off");
    CA_CHECK_EQ(4, cap.count);

    /* Invalid levels are ignored. */
    ca_logger_set_level(logger, (ca_log_level)99);
    CA_CHECK_EQ(CA_LOG_OFF, ca_logger_get_level(logger));

    ca_logger_destroy(&logger);
    CA_CHECK(logger == nullptr);

    capture_destroy(&cap);
    return ca_test_finish("log.levels");
}

static int test_explicit_length(void) {
    capture cap;
    capture_init(&cap);

    ca_logger* logger = nullptr;
    CA_CHECK(make_capture_logger(&logger, &cap, CA_LOG_INFO));

    const char payload[] = {'a', '\0', 'b'};
    ca_logger_write(logger, CA_LOG_INFO, payload, sizeof(payload));

    CA_CHECK_EQ(1, cap.count);
    CA_CHECK_EQ((long long)sizeof(payload), (long long)cap.len);
    CA_CHECK(cap.text[0] == 'a' && cap.text[1] == '\0' && cap.text[2] == 'b');

    /* Empty records still reach the sink. */
    size_t before = cap.len;
    ca_logger_write(logger, CA_LOG_INFO, "", 0);
    CA_CHECK_EQ(2, cap.count);
    CA_CHECK_EQ(before, cap.len);

    ca_logger_destroy(&logger);
    capture_destroy(&cap);
    return ca_test_finish("log.explicit_length");
}

static int test_long_format(void) {
    capture cap;
    capture_init(&cap);

    ca_logger* logger = nullptr;
    CA_CHECK(make_capture_logger(&logger, &cap, CA_LOG_INFO));

    char long_text[2048];
    memset(long_text, 'x', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';

    ca_logger_log(logger, CA_LOG_INFO, "%s", long_text);

    CA_CHECK_EQ(1, cap.count);
    CA_CHECK_EQ((long long)(sizeof(long_text) - 1), (long long)cap.len);
    CA_CHECK_EQ(0, memcmp(cap.text, long_text, sizeof(long_text) - 1));

    ca_logger_destroy(&logger);
    capture_destroy(&cap);
    return ca_test_finish("log.long_format");
}

static int test_sink_ownership(void) {
    int destroyed_first = 0;
    int destroyed_second = 0;

    ca_log_sink first = {noop_write, &destroyed_first, count_destroy};
    ca_log_config config;
    ca_log_config_init(&config);
    config.level = CA_LOG_INFO;
    config.sink = first;

    ca_error err;
    ca_error_init(&err);

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));
    CA_CHECK(logger != nullptr);

    ca_log_sink second = {noop_write, &destroyed_second, count_destroy};
    CA_CHECK_EQ(CA_OK, ca_logger_set_sink(logger, &second, &err));
    CA_CHECK_EQ(1, destroyed_first);
    CA_CHECK_EQ(0, destroyed_second);

    CA_CHECK_EQ(CA_OK, ca_logger_set_sink(logger, nullptr, &err));
    CA_CHECK_EQ(1, destroyed_second);

    ca_logger_destroy(&logger);
    CA_CHECK_EQ(1, destroyed_first);
    CA_CHECK_EQ(1, destroyed_second);

    return ca_test_finish("log.sink_ownership");
}

typedef struct reentrant_ctx {
    ca_logger* logger;
    int count;
} reentrant_ctx;

static void reentrant_write(void* ctx, ca_log_level level, const char* message, size_t len) {
    (void)level;
    (void)message;
    (void)len;

    reentrant_ctx* state = (reentrant_ctx*)ctx;
    state->count++;

    /* Reading the level and logging again must not deadlock: the logger
     * releases its lock before invoking the sink. */
    (void)ca_logger_get_level(state->logger);
    if (state->count == 1) {
        ca_logger_write(state->logger, CA_LOG_INFO, "nested", 6);
    }
}

static int test_reentrancy(void) {
    ca_log_config config;
    ca_log_config_init(&config);
    config.level = CA_LOG_INFO;

    ca_error err;
    ca_error_init(&err);

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));

    reentrant_ctx state = {logger, 0};
    ca_log_sink sink = {reentrant_write, &state, nullptr};
    CA_CHECK_EQ(CA_OK, ca_logger_set_sink(logger, &sink, &err));

    ca_logger_log(logger, CA_LOG_INFO, "outer");
    CA_CHECK_EQ(2, state.count);

    ca_logger_destroy(&logger);
    return ca_test_finish("log.reentrancy");
}

typedef struct thread_state {
    ca_logger* logger;
    int iterations;
} thread_state;

static _Atomic int thread_count = 0;

static void count_write(void* ctx, ca_log_level level, const char* message, size_t len) {
    (void)ctx;
    (void)level;
    (void)message;
    (void)len;
    (void)atomic_fetch_add(&thread_count, 1);
}

static void* thread_body(void* arg) {
    thread_state* state = (thread_state*)arg;

    for (int i = 0; i < state->iterations; ++i) {
        ca_logger_log(state->logger, CA_LOG_INFO, "thread %d message %d", i, i);
    }

    return nullptr;
}

static int test_threaded(void) {
    ca_log_config config;
    ca_log_config_init(&config);
    config.level = CA_LOG_INFO;
    config.sink.write = count_write;

    ca_error err;
    ca_error_init(&err);

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));

    atomic_store(&thread_count, 0);

    enum { THREADS = 4, ITERATIONS = 250 };
    ca_thread_t threads[THREADS] = {0};
    thread_state states[THREADS];

    int created = 0;
    for (int i = 0; i < THREADS; ++i) {
        states[i].logger = logger;
        states[i].iterations = ITERATIONS;
        if (ca_thread_create(&threads[i], thread_body, &states[i], &err) == CA_OK) {
            created++;
        }
    }

    CA_CHECK_EQ(THREADS, created);

    for (int i = 0; i < created; ++i) {
        CA_CHECK_EQ(CA_OK, ca_thread_join(threads[i], nullptr, &err));
    }

    CA_CHECK_EQ(THREADS * ITERATIONS, atomic_load(&thread_count));

    ca_logger_destroy(&logger);
    return ca_test_finish("log.threaded");
}

typedef struct race_sink_ctx {
    _Atomic int calls;
} race_sink_ctx;

static _Atomic int race_destroyed = 0;

static void race_sink_write(void* ctx, ca_log_level level, const char* message, size_t len) {
    (void)level;
    (void)message;
    (void)len;

    race_sink_ctx* state = (race_sink_ctx*)ctx;
    (void)atomic_fetch_add(&state->calls, 1);
}

static void race_sink_destroy(void* ctx) {
    free(ctx);
    (void)atomic_fetch_add(&race_destroyed, 1);
}

static void* race_writer(void* arg) {
    ca_logger* logger = (ca_logger*)arg;

    for (int i = 0; i < 2000; ++i) {
        ca_logger_log(logger, CA_LOG_INFO, "race %d", i);
    }

    return nullptr;
}

static int test_sink_replacement_race(void) {
    ca_error err;
    ca_error_init(&err);

    race_sink_ctx* initial = (race_sink_ctx*)malloc(sizeof(*initial));
    CA_CHECK(initial != nullptr);
    if (initial == nullptr) {
        return ca_test_finish("log.sink_race");
    }
    atomic_init(&initial->calls, 0);

    ca_log_config config;
    ca_log_config_init(&config);
    config.level = CA_LOG_INFO;
    config.sink.write = race_sink_write;
    config.sink.ctx = initial;
    config.sink.destroy = race_sink_destroy;

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));
    if (logger == nullptr) {
        free(initial);
        return ca_test_finish("log.sink_race");
    }

    atomic_store(&race_destroyed, 0);

    ca_thread_t writer = nullptr;
    CA_CHECK_EQ(CA_OK, ca_thread_create(&writer, race_writer, logger, &err));

    int replaced = 0;
    for (int i = 0; i < 200; ++i) {
        race_sink_ctx* ctx = (race_sink_ctx*)malloc(sizeof(*ctx));
        if (ctx == nullptr) {
            break;
        }
        atomic_init(&ctx->calls, 0);

        ca_log_sink sink = {race_sink_write, ctx, race_sink_destroy};
        if (ca_logger_set_sink(logger, &sink, &err) != CA_OK) {
            free(ctx);
            break;
        }
        replaced++;
    }

    if (writer) {
        CA_CHECK_EQ(CA_OK, ca_thread_join(writer, nullptr, &err));
    }

    /* Clearing the sink releases the last reference, so every context
     * allocated above is destroyed exactly once. */
    CA_CHECK_EQ(CA_OK, ca_logger_set_sink(logger, nullptr, &err));
    CA_CHECK_EQ(replaced + 1, atomic_load(&race_destroyed));

    ca_logger_destroy(&logger);
    return ca_test_finish("log.sink_race");
}

static size_t read_file(const char* path, char* buf, size_t size) {
    FILE* fp = fopen(path, "rb");
    if (fp == nullptr) {
        return 0;
    }

    size_t read = fread(buf, 1, size - 1, fp);
    (void)fclose(fp);
    buf[read] = '\0';
    return read;
}

static int count_lines(const char* path) {
    char buffer[16384];
    size_t len = read_file(path, buffer, sizeof(buffer));
    int lines = 0;

    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] == '\n') {
            lines++;
        }
    }

    return lines;
}

static int test_file_sink(void) {
    (void)remove(LOG_FILE);

    ca_error err;
    ca_error_init(&err);

    ca_log_file* file = nullptr;
    CA_CHECK_EQ(CA_OK, ca_log_file_open(&file, LOG_FILE, 0, 0, &err));
    CA_CHECK(file != nullptr);

    ca_log_config config;
    ca_log_config_init(&config);
    config.level = CA_LOG_INFO;
    config.sink = ca_log_file_sink(file);
    file = nullptr; /* ownership moved into the logger */

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));

    ca_logger_log(logger, CA_LOG_INFO, "first line");
    ca_logger_log(logger, CA_LOG_WARN, "second line");
    ca_logger_log(logger, CA_LOG_ERROR, "third line");
    ca_logger_destroy(&logger);

    char buffer[4096];
    size_t len = read_file(LOG_FILE, buffer, sizeof(buffer));
    CA_CHECK(len > 0);
    CA_CHECK(strstr(buffer, "[INFO]") != nullptr);
    CA_CHECK(strstr(buffer, "[WARN]") != nullptr);
    CA_CHECK(strstr(buffer, "[ERROR]") != nullptr);
    CA_CHECK(strstr(buffer, "first line") != nullptr);
    CA_CHECK(strstr(buffer, "second line") != nullptr);
    CA_CHECK(strstr(buffer, "third line") != nullptr);
    CA_CHECK_EQ(3, count_lines(LOG_FILE));

    /* UTC timestamp shape: [YYYY-MM-DDT...Z]. */
    CA_CHECK(buffer[0] == '[');
    CA_CHECK(buffer[1] == '2');
    CA_CHECK(buffer[5] == '-');
    CA_CHECK(buffer[8] == '-');
    CA_CHECK(buffer[11] == 'T');

    CA_CHECK_EQ(0, remove(LOG_FILE));
    return ca_test_finish("log.file_sink");
}

static int count_archived_lines(const char* path, unsigned archives) {
    int lines = count_lines(path);

    for (unsigned i = 1; i <= archives; ++i) {
        char name[256];
        (void)snprintf(name, sizeof(name), "%s.%u", path, i);
        lines += count_lines(name);
    }

    return lines;
}

static void remove_archives(const char* path, unsigned archives) {
    (void)remove(path);

    for (unsigned i = 1; i <= archives; ++i) {
        char name[256];
        (void)snprintf(name, sizeof(name), "%s.%u", path, i);
        (void)remove(name);
    }
}

static int test_file_rotation(void) {
    (void)remove_archives(ROTATE_FILE, 12);

    ca_error err;
    ca_error_init(&err);

    /* max_files is large enough to retain all 30 records. */
    ca_log_file* file = nullptr;
    CA_CHECK_EQ(CA_OK, ca_log_file_open(&file, ROTATE_FILE, 512, 10, &err));

    ca_log_config config;
    ca_log_config_init(&config);
    config.level = CA_LOG_INFO;
    config.sink = ca_log_file_sink(file);
    file = nullptr;

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));

    for (int i = 0; i < 30; ++i) {
        ca_logger_log(logger, CA_LOG_INFO, "rotation line %d", i);
    }
    ca_logger_destroy(&logger);

    FILE* check = fopen(ROTATE_FILE, "rb");
    CA_CHECK(check != nullptr);
    if (check) {
        (void)fclose(check);
    }

    check = fopen(ROTATE_FILE ".1", "rb");
    CA_CHECK(check != nullptr);
    if (check) {
        (void)fclose(check);
    }

    /* Archives beyond max_files never exist. */
    check = fopen(ROTATE_FILE ".10", "rb");
    CA_CHECK(check == nullptr);
    if (check) {
        (void)fclose(check);
    }

    CA_CHECK_EQ(30, count_archived_lines(ROTATE_FILE, 10));

    remove_archives(ROTATE_FILE, 12);
    return ca_test_finish("log.file_rotation");
}

static int test_file_rotation_limit(void) {
    const char* path = "caudio_test_log_rotate_limit.log";
    (void)remove_archives(path, 4);

    ca_error err;
    ca_error_init(&err);

    ca_log_file* file = nullptr;
    CA_CHECK_EQ(CA_OK, ca_log_file_open(&file, path, 128, 2, &err));

    ca_log_config config;
    ca_log_config_init(&config);
    config.level = CA_LOG_INFO;
    config.sink = ca_log_file_sink(file);
    file = nullptr;

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));

    for (int i = 0; i < 30; ++i) {
        ca_logger_log(logger, CA_LOG_INFO, "limited rotation %d", i);
    }
    ca_logger_destroy(&logger);

    FILE* check = fopen(path, "rb");
    CA_CHECK(check != nullptr);
    if (check) {
        (void)fclose(check);
    }

    check = fopen("caudio_test_log_rotate_limit.log.2", "rb");
    CA_CHECK(check != nullptr);
    if (check) {
        (void)fclose(check);
    }

    check = fopen("caudio_test_log_rotate_limit.log.3", "rb");
    CA_CHECK(check == nullptr);
    if (check) {
        (void)fclose(check);
    }

    /* The archive cap drops the oldest records. */
    int lines = count_archived_lines(path, 2);
    CA_CHECK(lines > 0);
    CA_CHECK(lines < 30);

    remove_archives(path, 4);
    return ca_test_finish("log.file_rotation_limit");
}

static int test_file_truncate_rotation(void) {
    const char* path = "caudio_test_log_truncate.log";
    (void)remove(path);
    (void)remove("caudio_test_log_truncate.log.1");

    ca_error err;
    ca_error_init(&err);

    ca_log_file* file = nullptr;
    CA_CHECK_EQ(CA_OK, ca_log_file_open(&file, path, 128, 0, &err));

    ca_log_config config;
    ca_log_config_init(&config);
    config.level = CA_LOG_INFO;
    config.sink = ca_log_file_sink(file);
    file = nullptr;

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));

    for (int i = 0; i < 20; ++i) {
        ca_logger_log(logger, CA_LOG_INFO, "truncate line %d", i);
    }
    ca_logger_destroy(&logger);

    FILE* check = fopen(path, "rb");
    CA_CHECK(check != nullptr);
    if (check) {
        (void)fclose(check);
    }

    check = fopen("caudio_test_log_truncate.log.1", "rb");
    CA_CHECK(check == nullptr);
    if (check) {
        (void)fclose(check);
    }

    (void)remove(path);
    return ca_test_finish("log.file_truncate_rotation");
}

static int test_invalid_args(void) {
    ca_error err;
    ca_error_init(&err);

    ca_logger* logger = nullptr;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_logger_create(nullptr, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, err.code);

    ca_log_config config;
    ca_log_config_init(&config);
    config.level = (ca_log_level)99;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_logger_create(&logger, &config, &err));
    CA_CHECK(logger == nullptr);

    ca_log_config_init(&config);
    config.sink.ctx = &config;
    config.sink.destroy = count_destroy;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_logger_create(&logger, &config, &err));
    CA_CHECK(logger == nullptr);

    ca_log_config_init(&config);
    CA_CHECK_EQ(CA_OK, ca_logger_create(&logger, &config, &err));
    CA_CHECK(logger != nullptr);

    ca_log_sink bad_sink = {nullptr, &config, count_destroy};
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_logger_set_sink(logger, &bad_sink, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_logger_set_sink(nullptr, &bad_sink, &err));

    ca_logger_write(nullptr, CA_LOG_INFO, "ignored", 7);
    ca_logger_log(nullptr, CA_LOG_INFO, "%s", "ignored");
    ca_logger_write(logger, CA_LOG_INFO, nullptr, 0);
    CA_CHECK_EQ(CA_LOG_OFF, ca_logger_get_level(nullptr));

    ca_logger_destroy(&logger);
    ca_logger_destroy(nullptr);
    ca_logger_destroy(&logger);

    ca_log_file* file = (ca_log_file*)(uintptr_t)1;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_log_file_open(nullptr, "x", 0, 0, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_log_file_open(&file, nullptr, 0, 0, &err));
    CA_CHECK(file == nullptr);

    file = nullptr;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_log_file_open(&file, "x", 0, 1001, &err));
    CA_CHECK(file == nullptr);

    file = nullptr;
    CA_CHECK_EQ(CA_IO_ERROR, ca_log_file_open(&file, "caudio_missing_dir/log.txt", 0, 0, &err));
    CA_CHECK(file == nullptr);

    ca_log_file_close(nullptr);
    ca_log_file_close(&file);

    ca_log_sink empty = ca_log_file_sink(nullptr);
    CA_CHECK(empty.write == nullptr);
    CA_CHECK(empty.ctx == nullptr);
    CA_CHECK(empty.destroy == nullptr);

    return ca_test_finish("log.invalid_args");
}

int main(void) {
    int failures = 0;

    failures += test_config_init();
    failures += test_level_names();
    failures += test_levels_and_callback();
    failures += test_explicit_length();
    failures += test_long_format();
    failures += test_sink_ownership();
    failures += test_reentrancy();
    failures += test_threaded();
    failures += test_sink_replacement_race();
    failures += test_file_sink();
    failures += test_file_rotation();
    failures += test_file_rotation_limit();
    failures += test_file_truncate_rotation();
    failures += test_invalid_args();

    return failures;
}
