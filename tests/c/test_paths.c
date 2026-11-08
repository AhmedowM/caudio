#include <caudio/config/paths.h>
#include <caudio/runtime/env.h>
#include <caudio/runtime/fs.h>
#include <caudio/runtime/path.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>
#include <string.h>

#include "ca_test.h"

static bool str_eq(const cx_string* value, const char* expected) {
    if (!value) {
        return expected == nullptr || expected[0] == '\0';
    }
    return cx_string_view_eq(cx_string_view_of(value), cx_string_view_from_cstr(expected));
}

static bool ends_with(const cx_string* value, const char* suffix) {
    size_t value_len = cx_string_length(value);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > value_len) {
        return false;
    }
    return memcmp(cx_string_cstr(value) + value_len - suffix_len, suffix, suffix_len) == 0;
}

static bool contains(const cx_string* value, const char* needle) {
    return strstr(cx_string_cstr(value), needle) != nullptr;
}

static void test_hex8(void) {
    char hex[9];

    ca_paths_hex8_for_db("", hex);
    CA_CHECK(strcmp(hex, "4fd0bfc1") == 0);

    ca_paths_hex8_for_db("abc", hex);
    CA_CHECK(strcmp(hex, "e25ef552") == 0);

    ca_paths_hex8_for_db("/music/a.mp3", hex);
    CA_CHECK(strcmp(hex, "16253739") == 0);

    /* Separators share one identity on every platform. */
    char slash[9];
    char backslash[9];
    ca_paths_hex8_for_db("/Music/a.mp3", slash);
    ca_paths_hex8_for_db("\\Music\\a.mp3", backslash);
    CA_CHECK(strcmp(slash, backslash) == 0);

#ifdef _WIN32
    /* Windows identities fold ASCII case. */
    char lower[9];
    ca_paths_hex8_for_db("/music/a.mp3", lower);
    CA_CHECK(strcmp(lower, "16253739") == 0);
#else
    char lower[9];
    ca_paths_hex8_for_db("/music/a.mp3", lower);
    ca_paths_hex8_for_db("/MUSIC/A.MP3", hex);
    CA_CHECK(strcmp(lower, hex) != 0);
#endif

    /* nullptr is the empty path. */
    ca_paths_hex8_for_db(nullptr, hex);
    CA_CHECK(strcmp(hex, "4fd0bfc1") == 0);
}

static void set_env(const char* name, const char* value) {
    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_env_set(name, value, true, &err));
}

static void test_default_paths(const cx_allocator* alloc) {
    /* Pin every discovery variable to a controlled tree. */
    set_env("LOCALAPPDATA", "caudio_test_paths_tmp/localapp");
    set_env("XDG_DATA_HOME", "caudio_test_paths_tmp/data");
    set_env("XDG_CONFIG_HOME", "caudio_test_paths_tmp/config");
    set_env("XDG_RUNTIME_DIR", "caudio_test_paths_tmp/run");
    set_env("HOME", "caudio_test_paths_tmp/home");
    set_env("USERPROFILE", "caudio_test_paths_tmp/home");
    set_env("TMPDIR", "caudio_test_paths_tmp/tmp");

    cx_string* db = nullptr;
    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_paths_default_db_path(&db, alloc, &err));
    CA_CHECK(ends_with(db, "/caudio/library.db"));
    CA_CHECK(strstr(cx_string_cstr(db), "\\") == nullptr);
#ifdef _WIN32
    CA_CHECK(contains(db, "localapp/caudio/library.db"));
#else
    CA_CHECK(contains(db, "data/caudio/library.db"));
#endif
    cx_string_destroy(&db);

    cx_string* config = nullptr;
    CA_CHECK_EQ(CA_OK, ca_paths_default_config_path(&config, alloc, &err));
    CA_CHECK(ends_with(config, "/caudio/config.json"));
#ifdef _WIN32
    CA_CHECK(contains(config, "localapp/caudio/config.json"));
#else
    CA_CHECK(contains(config, "config/caudio/config.json"));
#endif
    cx_string_destroy(&config);

    cx_string* legacy = nullptr;
    CA_CHECK_EQ(CA_OK, ca_paths_legacy_config_path(&legacy, alloc));
#ifdef _WIN32
    CA_CHECK(str_eq(legacy, "caudio_test_paths_tmp/home/.config/caudio/config.json"));
#else
    CA_CHECK(str_eq(legacy, ""));
#endif
    cx_string_destroy(&legacy);

    /* nullptr out is rejected. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_paths_default_db_path(nullptr, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_paths_default_config_path(nullptr, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_paths_legacy_config_path(nullptr, alloc));
}

static void test_derived_paths(const cx_allocator* alloc) {
    char hex[9];
    ca_paths_hex8_for_db("/music/library.db", hex);

    char expected[64];
    ca_error err;

    cx_string* socket = nullptr;
    CA_CHECK_EQ(CA_OK, ca_paths_socket_for("/music/library.db", &socket, alloc, &err));
#ifdef _WIN32
    (void)expected;
    CA_CHECK_EQ(0, strncmp(cx_string_cstr(socket), "\\\\.\\pipe\\caudio-", 16));
    CA_CHECK(strstr(cx_string_cstr(socket), hex) != nullptr);
#else
    (void)snprintf(expected, sizeof expected, "caudio-%s.sock", hex);
    CA_CHECK(ends_with(socket, expected));
#endif
    cx_string_destroy(&socket);

    cx_string* pid = nullptr;
    CA_CHECK_EQ(CA_OK, ca_paths_pid_for("/music/library.db", &pid, alloc, &err));
    (void)snprintf(expected, sizeof expected, "caudio-%s.pid", hex);
    CA_CHECK(ends_with(pid, expected));
    cx_string_destroy(&pid);

    cx_string* lock = nullptr;
    CA_CHECK_EQ(CA_OK, ca_paths_lock_for("/music/library.db", &lock, alloc, &err));
    (void)snprintf(expected, sizeof expected, "caudio-%s.lock", hex);
    CA_CHECK(ends_with(lock, expected));
    cx_string_destroy(&lock);

    cx_string* log = nullptr;
    CA_CHECK_EQ(CA_OK, ca_paths_log_for("/music/library.db", &log, alloc, &err));
    CA_CHECK(str_eq(log, "/music/caudio.log"));
    cx_string_destroy(&log);

    /* A database with no parent falls back to the temporary directory. */
    CA_CHECK_EQ(CA_OK, ca_paths_log_for("library.db", &log, alloc, &err));
    CA_CHECK(ends_with(log, "/caudio.log"));
    cx_string_destroy(&log);

    /* nullptr database is treated as empty. */
    CA_CHECK_EQ(CA_OK, ca_paths_pid_for(nullptr, &pid, alloc, &err));
    CA_CHECK(ends_with(pid, ".pid"));
    cx_string_destroy(&pid);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_paths_socket_for("/x", nullptr, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_paths_pid_for("/x", nullptr, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_paths_lock_for("/x", nullptr, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_paths_log_for("/x", nullptr, alloc, &err));
}

static void test_runtime_path_helpers(void) {
    ca_error err;
    size_t length = 0;
    CA_CHECK_EQ(CA_OK, ca_path_temp_dir(nullptr, 0, &length, &err));
    CA_CHECK(length > 0);
    CA_CHECK(length < 4096);

    char buffer[4096];
    CA_CHECK_EQ(CA_OK, ca_path_temp_dir(buffer, sizeof buffer, &length, &err));
    CA_CHECK_EQ(strlen(buffer), length);
    CA_CHECK(strstr(buffer, "\\") == nullptr);
    if (length > 1) {
        CA_CHECK(buffer[length - 1] != '/');
    }

    /* A short buffer reports overflow and the required length. */
    char tiny[2];
    size_t needed = 0;
    CA_CHECK_EQ(CA_OVERFLOW, ca_path_temp_dir(tiny, sizeof tiny, &needed, &err));
    CA_CHECK_EQ(needed, length);
    CA_CHECK_EQ('\0', tiny[0]);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_path_temp_dir(nullptr, 4, nullptr, &err));

    /* Identity normalization folds separators everywhere and ASCII case on
     * Windows. */
    char identity[] = "C:\\Music\\Song.MP3";
    CA_CHECK(ca_path_identity_normalize(identity) == identity);
#ifdef _WIN32
    CA_CHECK(strcmp(identity, "c:/music/song.mp3") == 0);
#else
    CA_CHECK(strcmp(identity, "C:/Music/Song.MP3") == 0);
#endif
    CA_CHECK(ca_path_identity_normalize(nullptr) == nullptr);
}

static void test_runtime_paths_lifecycle(const cx_allocator* alloc) {
    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs("caudio_test_paths_tmp", &err));

    ca_runtime_paths paths;
    CA_CHECK_EQ(CA_OK, ca_runtime_paths_init(&paths, alloc));
    CA_CHECK(ca_runtime_paths_empty(&paths));

    CA_CHECK_EQ(CX_OK,
                cx_string_from(&paths.database, "caudio_test_paths_tmp/db/library.db", alloc));
    CA_CHECK_EQ(CX_OK, cx_string_from(&paths.configuration, "caudio_test_paths_tmp/cfg/config.json",
                                      alloc));
    CA_CHECK_EQ(CX_OK, cx_string_from(&paths.logs, "caudio_test_paths_tmp/logs", alloc));
    CA_CHECK_EQ(CX_OK, cx_string_from(&paths.cache, "caudio_test_paths_tmp/cache", alloc));
    CA_CHECK_EQ(CX_OK, cx_string_from(&paths.imported_media, "caudio_test_paths_tmp/media", alloc));
    CA_CHECK_EQ(CX_OK, cx_string_from(&paths.socket, "explicit.sock", alloc));
    CA_CHECK(!ca_runtime_paths_empty(&paths));

    CA_CHECK_EQ(CA_OK, ca_runtime_paths_ensure_directories(&paths, &err));
    CA_CHECK(ca_fs_exists("caudio_test_paths_tmp/db", &err));
    CA_CHECK(ca_fs_exists("caudio_test_paths_tmp/cfg", &err));
    CA_CHECK(ca_fs_exists("caudio_test_paths_tmp/logs", &err));
    CA_CHECK(ca_fs_exists("caudio_test_paths_tmp/cache", &err));
    CA_CHECK(ca_fs_exists("caudio_test_paths_tmp/media", &err));

    ca_runtime_paths copy;
    CA_CHECK_EQ(CA_OK, ca_runtime_paths_copy(&copy, &paths, alloc));
    CA_CHECK(str_eq(copy.database, "caudio_test_paths_tmp/db/library.db"));
    CA_CHECK(str_eq(copy.socket, "explicit.sock"));
    CA_CHECK(copy.database != paths.database);

    /* Deep copies do not share storage. */
    CA_CHECK_EQ(CX_OK, cx_string_assign(copy.database, "changed"));
    CA_CHECK(str_eq(paths.database, "caudio_test_paths_tmp/db/library.db"));

    ca_runtime_paths moved;
    CA_CHECK_EQ(CA_OK, ca_runtime_paths_init(&moved, alloc));
    ca_runtime_paths_move(&moved, &copy);
    CA_CHECK(str_eq(moved.database, "changed"));
    CA_CHECK(copy.database == nullptr);
    CA_CHECK(ca_runtime_paths_empty(&copy));

    ca_runtime_paths_destroy(&moved);
    ca_runtime_paths_destroy(&paths);

    /* nullptr-safe behaviour. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_runtime_paths_init(nullptr, alloc));
    ca_runtime_paths_destroy(nullptr);
    CA_CHECK(ca_runtime_paths_empty(nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_runtime_paths_ensure_directories(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_runtime_paths_copy(nullptr, &paths, alloc));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_runtime_paths_copy(&copy, nullptr, alloc));

    ca_runtime_paths empty;
    CA_CHECK_EQ(CA_OK, ca_runtime_paths_init(&empty, alloc));
    CA_CHECK_EQ(CA_OK, ca_runtime_paths_ensure_directories(&empty, &err));
    ca_runtime_paths_destroy(&empty);
}

static void test_leak_free(void) {
    cx_allocator_counted counted;
    CA_CHECK_EQ(CX_OK, cx_allocator_counted_init(&counted, nullptr));
    cx_allocator alloc = cx_allocator_counted_as_allocator(&counted);

    cx_string* db = nullptr;
    CA_CHECK_EQ(CA_OK, ca_paths_default_db_path(&db, &alloc, nullptr));
    cx_string_destroy(&db);

    cx_string* socket = nullptr;
    CA_CHECK_EQ(CA_OK, ca_paths_socket_for("/a/b", &socket, &alloc, nullptr));
    cx_string_destroy(&socket);

    ca_runtime_paths paths;
    CA_CHECK_EQ(CA_OK, ca_runtime_paths_init(&paths, &alloc));
    CA_CHECK_EQ(CX_OK, cx_string_from(&paths.logs, "/tmp/logs", &alloc));
    ca_runtime_paths copy;
    CA_CHECK_EQ(CA_OK, ca_runtime_paths_copy(&copy, &paths, &alloc));
    ca_runtime_paths_destroy(&copy);
    ca_runtime_paths_destroy(&paths);

    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));
    CA_CHECK_EQ(0, counted.live_blocks);
    cx_allocator_counted_destroy(&counted);
}

int main(void) {
    cx_allocator_counted counted;
    CA_CHECK_EQ(CX_OK, cx_allocator_counted_init(&counted, nullptr));
    cx_allocator alloc = cx_allocator_counted_as_allocator(&counted);

    test_hex8();
    test_default_paths(&alloc);
    test_derived_paths(&alloc);
    test_runtime_path_helpers();
    test_runtime_paths_lifecycle(&alloc);

    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));
    CA_CHECK_EQ(0, counted.live_blocks);
    cx_allocator_counted_destroy(&counted);

    test_leak_free();
    return ca_test_finish("paths");
}
