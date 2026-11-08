#include <caudio/config/config.h>
#include <caudio/runtime/env.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/fs.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>
#include <cstdx/vector.h>
#include <string.h>

#include "ca_test.h"

static bool str_eq(const cx_string* value, const char* expected) {
    if (!value) {
        return expected == nullptr || expected[0] == '\0';
    }
    return cx_string_view_eq(cx_string_view_of(value), cx_string_view_from_cstr(expected));
}

static bool write_file(const char* path, const char* text) {
    ca_error err;
    ca_file_t file = ca_fs_open(path, CA_FS_WRITE | CA_FS_CREATE | CA_FS_TRUNCATE, &err);
    if (file == CA_FS_INVALID) {
        return false;
    }
    size_t length = strlen(text);
    size_t wrote = ca_fs_write(file, text, length, &err);
    ca_fs_close(file);
    return wrote == length;
}

static bool read_file(const char* path, char* buffer, size_t capacity) {
    ca_error err;
    ca_file_t file = ca_fs_open(path, CA_FS_READ, &err);
    if (file == CA_FS_INVALID) {
        return false;
    }
    size_t got = ca_fs_read(file, buffer, capacity - 1, &err);
    ca_fs_close(file);
    if (got == (size_t)-1 || got >= capacity) {
        return false;
    }
    buffer[got] = '\0';
    return true;
}

static void assign(cx_string* str, const char* text) {
    CA_CHECK_EQ(CX_OK, cx_string_assign(str, text));
}

static void test_defaults(void) {
    ca_config config;
    CA_CHECK_EQ(CA_OK, ca_config_init(&config, nullptr));
    CA_CHECK(str_eq(config.db_path, ""));
    CA_CHECK(str_eq(config.config_path, ""));
    CA_CHECK(str_eq(config.device, "auto"));
    CA_CHECK_EQ(2, config.log_level);
    CA_CHECK(str_eq(config.socket_path, ""));
    CA_CHECK(!config.auto_resume_on_play);
    CA_CHECK(config.clear_stops_playback);
    CA_CHECK(str_eq(config.repeat_default, "off"));
    CA_CHECK(!config.autostart);

    ca_config copy;
    CA_CHECK_EQ(CA_OK, ca_config_copy(&copy, &config, nullptr));
    CA_CHECK(str_eq(copy.device, "auto"));
    CA_CHECK(str_eq(copy.repeat_default, "off"));
    CA_CHECK_EQ(2, copy.log_level);
    ca_config_destroy(&copy);
    ca_config_destroy(&config);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_init(nullptr, nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_copy(nullptr, &config, nullptr));
    ca_config_destroy(nullptr);
}

static void test_load_missing(cx_allocator* alloc) {
    ca_config config;
    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_config_load(&config, "caudio_test_config_tmp/missing.json", alloc, &err));
    CA_CHECK(str_eq(config.config_path, "caudio_test_config_tmp/missing.json"));
    CA_CHECK(cx_string_length(config.db_path) > 0);
    CA_CHECK(str_eq(config.device, "auto"));
    CA_CHECK_EQ(2, config.log_level);
    CA_CHECK(config.clear_stops_playback);
    ca_config_destroy(&config);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_load(nullptr, nullptr, alloc, &err));
}

static void test_load_full(cx_allocator* alloc) {
    const char* path = "caudio_test_config_tmp/full.json";
    const char* json = "{\n"
                       "  \"dbPath\": \"/from/new.db\",\n"
                       "  \"configPath\": \"/ignored/config.json\",\n"
                       "  \"device\": \"hw:1\",\n"
                       "  \"logLevel\": 3,\n"
                       "  \"socketPath\": \"/tmp/one.sock\",\n"
                       "  \"autoResumeOnPlay\": true,\n"
                       "  \"clearStopsPlayback\": false,\n"
                       "  \"repeatDefault\": \"all\",\n"
                       "  \"autostart\": true,\n"
                       "  \"db_path\": \"/from/legacy.db\",\n"
                       "  \"log_level\": \"error\"\n"
                       "}";
    CA_CHECK(write_file(path, json));

    ca_config config;
    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_config_load(&config, path, alloc, &err));
    /* Legacy keys win over the canonical names. */
    CA_CHECK(str_eq(config.db_path, "/from/legacy.db"));
    CA_CHECK(str_eq(config.config_path, path));
    CA_CHECK(str_eq(config.device, "hw:1"));
    CA_CHECK_EQ(4, config.log_level);
    CA_CHECK(str_eq(config.socket_path, "/tmp/one.sock"));
    CA_CHECK(config.auto_resume_on_play);
    CA_CHECK(!config.clear_stops_playback);
    CA_CHECK(str_eq(config.repeat_default, "all"));
    CA_CHECK(config.autostart);
    ca_config_destroy(&config);
}

static void test_load_tolerant(cx_allocator* alloc) {
    const char* path = "caudio_test_config_tmp/tolerant.json";
    const char* json = "{\n"
                       "  \"dbPath\": \"\",\n"
                       "  \"device\": 42,\n"
                       "  \"logLevel\": \"debug\",\n"
                       "  \"socketPath\": \"\",\n"
                       "  \"autoResumeOnPlay\": \"yes\",\n"
                       "  \"clearStopsPlayback\": 1,\n"
                       "  \"repeatDefault\": \"weird\",\n"
                       "  \"autostart\": false,\n"
                       "  \"db_path\": 7,\n"
                       "  \"log_level\": \"4\"\n"
                       "}";
    CA_CHECK(write_file(path, json));

    ca_config config;
    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_config_load(&config, path, alloc, &err));
    CA_CHECK(cx_string_length(config.db_path) > 0);
    CA_CHECK(str_eq(config.device, "auto"));
    CA_CHECK_EQ(4, config.log_level);
    CA_CHECK(str_eq(config.socket_path, ""));
    CA_CHECK(!config.auto_resume_on_play);
    CA_CHECK(config.clear_stops_playback);
    CA_CHECK(str_eq(config.repeat_default, "off"));
    CA_CHECK(!config.autostart);
    ca_config_destroy(&config);

    /* A hand-written numeric log level is accepted too. */
    CA_CHECK(write_file(path, "{\"logLevel\": \"2\"}"));
    CA_CHECK_EQ(CA_OK, ca_config_load(&config, path, alloc, &err));
    CA_CHECK_EQ(2, config.log_level);
    ca_config_destroy(&config);

    /* Unknown names leave the default in place. */
    CA_CHECK(write_file(path, "{\"logLevel\": \"loud\"}"));
    CA_CHECK_EQ(CA_OK, ca_config_load(&config, path, alloc, &err));
    CA_CHECK_EQ(2, config.log_level);
    ca_config_destroy(&config);
}

static void test_load_errors(cx_allocator* alloc) {
    ca_error err;
    ca_config config;

    const char* path = "caudio_test_config_tmp/bad.json";
    CA_CHECK(write_file(path, "{ nope"));
    CA_CHECK_EQ(CA_JSON_ERROR, ca_config_load(&config, path, alloc, &err));

    CA_CHECK(write_file(path, "[1,2,3]"));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_load(&config, path, alloc, &err));

    /* An empty file is treated as "use defaults". */
    CA_CHECK(write_file(path, ""));
    CA_CHECK_EQ(CA_OK, ca_config_load(&config, path, alloc, &err));
    CA_CHECK_EQ(2, config.log_level);
    ca_config_destroy(&config);

    /* Explicit malformed path with nullptr out. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_load(nullptr, path, alloc, &err));
}

static void test_save_roundtrip(cx_allocator* alloc) {
    ca_config config;
    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_config_init(&config, alloc));
    assign(config.config_path, "caudio_test_config_tmp/saved.json");
    assign(config.db_path, "/music/library.db");
    assign(config.device, "hw:1");
    config.log_level = 3;
    assign(config.socket_path, "/tmp/s.sock");
    config.auto_resume_on_play = true;
    config.clear_stops_playback = false;
    assign(config.repeat_default, "all");
    config.autostart = true;

    CA_CHECK_EQ(CA_OK, ca_config_save(&config, alloc, &err));

    char text[2048];
    CA_CHECK(read_file("caudio_test_config_tmp/saved.json", text, sizeof text));
    CA_CHECK(strcmp(text, "{\n"
                          "  \"dbPath\": \"/music/library.db\",\n"
                          "  \"device\": \"hw:1\",\n"
                          "  \"logLevel\": 3,\n"
                          "  \"autoResumeOnPlay\": true,\n"
                          "  \"clearStopsPlayback\": false,\n"
                          "  \"repeatDefault\": \"all\",\n"
                          "  \"autostart\": true,\n"
                          "  \"socketPath\": \"/tmp/s.sock\"\n"
                          "}") == 0);

    ca_config loaded;
    CA_CHECK_EQ(CA_OK, ca_config_load(&loaded, "caudio_test_config_tmp/saved.json", alloc, &err));
    CA_CHECK(str_eq(loaded.db_path, "/music/library.db"));
    CA_CHECK(str_eq(loaded.device, "hw:1"));
    CA_CHECK_EQ(3, loaded.log_level);
    CA_CHECK(str_eq(loaded.socket_path, "/tmp/s.sock"));
    CA_CHECK(loaded.auto_resume_on_play);
    CA_CHECK(!loaded.clear_stops_playback);
    CA_CHECK(str_eq(loaded.repeat_default, "all"));
    CA_CHECK(loaded.autostart);
    ca_config_destroy(&loaded);
    ca_config_destroy(&config);

    /* socketPath is omitted when empty. */
    CA_CHECK_EQ(CA_OK, ca_config_init(&config, alloc));
    assign(config.config_path, "caudio_test_config_tmp/no_socket.json");
    CA_CHECK_EQ(CA_OK, ca_config_save(&config, alloc, &err));
    CA_CHECK(read_file("caudio_test_config_tmp/no_socket.json", text, sizeof text));
    CA_CHECK(strstr(text, "socketPath") == nullptr);
    ca_config_destroy(&config);

    /* Saving without a config path is rejected by the filesystem layer. */
    CA_CHECK_EQ(CA_OK, ca_config_init(&config, alloc));
    CA_CHECK_EQ(CA_IO_ERROR, ca_config_save(&config, alloc, &err));
    ca_config_destroy(&config);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_save(nullptr, alloc, &err));
}

static void test_raw(cx_allocator* alloc) {
    const char* path = "caudio_test_config_tmp/raw.json";
    ca_error err;

    /* Setting into a missing file creates it. */
    CA_CHECK_EQ(CA_OK, ca_config_set_raw(path, "name", "Alice", alloc, &err));
    CA_CHECK_EQ(CA_OK, ca_config_set_raw(path, "count", "42", alloc, &err));
    CA_CHECK_EQ(CA_OK, ca_config_set_raw(path, "flag", "true", alloc, &err));
    CA_CHECK_EQ(CA_OK, ca_config_set_raw(path, "nested", "{\"a\":1}", alloc, &err));
    CA_CHECK_EQ(CA_OK, ca_config_set_raw(path, "nothing", "NULL", alloc, &err));

    cx_string* value = nullptr;
    CA_CHECK_EQ(CA_OK, ca_config_get_raw(path, "name", &value, alloc, &err));
    CA_CHECK(str_eq(value, "Alice"));
    cx_string_destroy(&value);

    CA_CHECK_EQ(CA_OK, ca_config_get_raw(path, "count", &value, alloc, &err));
    CA_CHECK(str_eq(value, "42"));
    cx_string_destroy(&value);

    CA_CHECK_EQ(CA_OK, ca_config_get_raw(path, "flag", &value, alloc, &err));
    CA_CHECK(str_eq(value, "true"));
    cx_string_destroy(&value);

    CA_CHECK_EQ(CA_OK, ca_config_get_raw(path, "nested", &value, alloc, &err));
    CA_CHECK(str_eq(value, "{\"a\":1}"));
    cx_string_destroy(&value);

    CA_CHECK_EQ(CA_OK, ca_config_get_raw(path, "nothing", &value, alloc, &err));
    CA_CHECK(str_eq(value, "NULL"));
    cx_string_destroy(&value);

    /* Listing preserves document order. */
    cx_vector* entries = nullptr;
    CA_CHECK_EQ(CA_OK, ca_config_list_raw(path, &entries, alloc, &err));
    CA_CHECK_EQ(5, cx_vector_length(entries));
    const char* expected_keys[] = {"name", "count", "flag", "nested", "nothing"};
    for (size_t i = 0; i < 5; ++i) {
        const ca_config_entry* entry = (const ca_config_entry*)cx_vector_ptr(entries, i);
        CA_CHECK(entry != nullptr);
        if (entry) {
            CA_CHECK(str_eq(entry->key, expected_keys[i]));
        }
    }
    ca_config_entries_destroy(&entries);

    /* "type" is hidden from list output but still readable. */
    CA_CHECK_EQ(CA_OK, ca_config_set_raw(path, "type", "\"cmd\"", alloc, &err));
    CA_CHECK_EQ(CA_OK, ca_config_get_raw(path, "type", &value, alloc, &err));
    CA_CHECK(str_eq(value, "cmd"));
    cx_string_destroy(&value);
    CA_CHECK_EQ(CA_OK, ca_config_list_raw(path, &entries, alloc, &err));
    CA_CHECK_EQ(5, cx_vector_length(entries));
    ca_config_entries_destroy(&entries);

    /* Overwriting an existing key keeps its position; "type" stays hidden. */
    CA_CHECK_EQ(CA_OK, ca_config_set_raw(path, "count", "7", alloc, &err));
    CA_CHECK_EQ(CA_OK, ca_config_list_raw(path, &entries, alloc, &err));
    CA_CHECK_EQ(5, cx_vector_length(entries));
    {
        const ca_config_entry* entry = (const ca_config_entry*)cx_vector_ptr(entries, 1);
        CA_CHECK(entry && str_eq(entry->key, "count") && str_eq(entry->value, "7"));
    }
    ca_config_entries_destroy(&entries);

    /* Deletion. */
    CA_CHECK_EQ(CA_OK, ca_config_delete_raw(path, "flag", alloc, &err));
    CA_CHECK_EQ(CA_NOT_FOUND, ca_config_get_raw(path, "flag", &value, alloc, &err));
    CA_CHECK_EQ(CA_NOT_FOUND, ca_config_delete_raw(path, "flag", alloc, &err));

    /* Reset removes the file. */
    CA_CHECK_EQ(CA_OK, ca_config_reset_all_raw(path, &err));
    CA_CHECK_EQ(CA_NOT_FOUND, ca_config_get_raw(path, "name", &value, alloc, &err));

    /* Unusable files. */
    const char* bad = "caudio_test_config_tmp/raw_bad.json";
    CA_CHECK(write_file(bad, "{bad"));
    CA_CHECK_EQ(CA_JSON_ERROR, ca_config_get_raw(bad, "x", &value, alloc, &err));
    CA_CHECK(ca_config_list_raw(bad, &entries, alloc, &err) == CA_JSON_ERROR);

    CA_CHECK(write_file(bad, "[1]"));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_get_raw(bad, "x", &value, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_delete_raw(bad, "x", alloc, &err));
    CA_CHECK_EQ(CA_OK, ca_config_list_raw(bad, &entries, alloc, &err));
    CA_CHECK_EQ(0, cx_vector_length(entries));
    ca_config_entries_destroy(&entries);

    /* A corrupt file is replaced by set_raw. */
    CA_CHECK_EQ(CA_OK, ca_config_set_raw(bad, "k", "v", alloc, &err));
    CA_CHECK_EQ(CA_OK, ca_config_get_raw(bad, "k", &value, alloc, &err));
    CA_CHECK(str_eq(value, "v"));
    cx_string_destroy(&value);
    CA_CHECK_EQ(CA_OK, ca_config_reset_all_raw(bad, &err));

    /* Argument validation. */
    CA_CHECK_EQ(CA_NOT_FOUND,
                ca_config_get_raw("caudio_test_config_tmp/none.json", "x", &value, alloc, &err));
    CA_CHECK_EQ(CA_OK,
                ca_config_list_raw("caudio_test_config_tmp/none.json", &entries, alloc, &err));
    CA_CHECK_EQ(0, cx_vector_length(entries));
    ca_config_entries_destroy(&entries);
    CA_CHECK_EQ(CA_OK, ca_config_reset_all_raw("caudio_test_config_tmp/none.json", &err));
    CA_CHECK_EQ(CA_OK, ca_config_reset_all_raw(nullptr, &err));

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_get_raw(path, nullptr, &value, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_get_raw(path, "k", nullptr, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_set_raw(path, nullptr, "v", alloc, &err));
    CA_CHECK_EQ(CA_IO_ERROR, ca_config_set_raw("", "k", "v", alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_list_raw(path, nullptr, alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_delete_raw(path, "", alloc, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_delete_raw(path, nullptr, alloc, &err));
    CA_CHECK_EQ(CA_NOT_FOUND,
                ca_config_delete_raw("caudio_test_config_tmp/none.json", "k", alloc, &err));
}

static void test_entry_lifecycle(cx_allocator* alloc) {
    ca_config_entry entry;
    CA_CHECK_EQ(CA_OK, ca_config_entry_init(&entry, alloc));
    assign(entry.key, "k");
    assign(entry.value, "v");

    ca_config_entry copy;
    CA_CHECK_EQ(CA_OK, ca_config_entry_copy(&copy, &entry, alloc));
    CA_CHECK(str_eq(copy.key, "k"));
    CA_CHECK(str_eq(copy.value, "v"));
    CA_CHECK(copy.key != entry.key);
    ca_config_entry_destroy(&copy);
    ca_config_entry_destroy(&entry);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_entry_init(nullptr, alloc));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_config_entry_copy(nullptr, &entry, alloc));
    ca_config_entry_destroy(nullptr);
    ca_config_entries_destroy(nullptr);
}

#ifdef _WIN32
static void test_legacy_fallback(cx_allocator* alloc) {
    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs("caudio_test_config_tmp/localapp", &err));
    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs("caudio_test_config_tmp/legacyhome/.config/caudio", &err));
    CA_CHECK(write_file("caudio_test_config_tmp/legacyhome/.config/caudio/"
                        "config.json",
                        "{\"log_level\": 4}"));

    CA_CHECK_EQ(CA_OK, ca_env_set("LOCALAPPDATA", "caudio_test_config_tmp/localapp", true, &err));
    CA_CHECK_EQ(CA_OK, ca_env_set("USERPROFILE", "caudio_test_config_tmp/legacyhome", true, &err));

    ca_config config;
    CA_CHECK_EQ(CA_OK, ca_config_load(&config, nullptr, alloc, &err));
    CA_CHECK_EQ(4, config.log_level);
    CA_CHECK(
        str_eq(config.config_path, "caudio_test_config_tmp/legacyhome/.config/caudio/config.json"));
    ca_config_destroy(&config);
}
#endif

int main(void) {
    cx_allocator_counted counted;
    CA_CHECK_EQ(CX_OK, cx_allocator_counted_init(&counted, nullptr));
    cx_allocator alloc = cx_allocator_counted_as_allocator(&counted);

    ca_error err;
    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs("caudio_test_config_tmp", &err));

    test_defaults();
    test_load_missing(&alloc);
    test_load_full(&alloc);
    test_load_tolerant(&alloc);
    test_load_errors(&alloc);
    test_save_roundtrip(&alloc);
    test_raw(&alloc);
    test_entry_lifecycle(&alloc);
#ifdef _WIN32
    test_legacy_fallback(&alloc);
#endif

    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));
    CA_CHECK_EQ(0, counted.live_blocks);
    cx_allocator_counted_destroy(&counted);
    return ca_test_finish("config");
}
