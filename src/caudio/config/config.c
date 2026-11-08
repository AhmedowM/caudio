#include <caudio/config/config.h>

#include <caudio/runtime/fs.h>
#include <caudio/runtime/json.h>

#include <cstdx/string_view.h>

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static ca_status ca_from_cx(cx_status status) {
    switch (status) {
    case CX_OK:
        return CA_OK;
    case CX_OUT_OF_MEMORY:
        return CA_OUT_OF_MEMORY;
    case CX_INVALID_ARGUMENT:
        return CA_INVALID_ARGUMENT;
    case CX_OVERFLOW:
        return CA_OVERFLOW;
    case CX_OUT_OF_RANGE:
        return CA_INVALID_ARGUMENT;
    default:
        return CA_INVALID_ARGUMENT;
    }
}

static ca_status text_assign(cx_string *str, const char *text) {
    return ca_from_cx(cx_string_assign_view(
        str, cx_string_view_from_cstr(text ? text : "")));
}

static ca_status text_assign_view(cx_string *str, const char *text,
                                  size_t length) {
    if (text == nullptr) {
        length = 0;
    }
    return ca_from_cx(
        cx_string_assign_view(str, cx_string_view_from_parts(text, length)));
}

static ca_status new_string(cx_string **out, const cx_allocator *alloc,
                            const char *initial) {
    *out = nullptr;
    ca_status status = ca_from_cx(cx_string_init(out, alloc));
    if (status != CA_OK) {
        return status;
    }
    status = text_assign(*out, initial);
    if (status != CA_OK) {
        cx_string_destroy(out);
    }
    return status;
}

static bool path_exists(const char *path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    ca_error err;
    return ca_fs_exists(path, &err);
}

static ca_status read_text_file(const char *path, cx_string **out,
                                const cx_allocator *alloc, ca_error *err) {
    *out = nullptr;

    ca_file_t file = ca_fs_open(path, CA_FS_READ, err);
    if (file == CA_FS_INVALID) {
        if (err && err->code != CA_OK) {
            return err->code;
        }
        return CA_IO_ERROR;
    }

    ca_status status = ca_from_cx(cx_string_init(out, alloc));
    if (status != CA_OK) {
        ca_fs_close(file);
        return status;
    }

    char buffer[4096];
    for (;;) {
        size_t got = ca_fs_read(file, buffer, sizeof buffer, err);
        if (got == (size_t)-1) {
            status = (err && err->code != CA_OK) ? err->code : CA_IO_ERROR;
            break;
        }
        if (got == 0) {
            break;
        }
        status = ca_from_cx(cx_string_append_view(
            *out, cx_string_view_from_parts(buffer, got)));
        if (status != CA_OK) {
            break;
        }
    }

    ca_fs_close(file);
    if (status != CA_OK) {
        cx_string_destroy(out);
    }
    return status;
}

static ca_status write_text_file(const char *path, const char *text,
                                 size_t length, ca_error *err) {
    ca_file_t file =
        ca_fs_open(path, CA_FS_WRITE | CA_FS_CREATE | CA_FS_TRUNCATE, err);
    if (file == CA_FS_INVALID) {
        if (err && err->code != CA_OK) {
            return err->code;
        }
        return CA_IO_ERROR;
    }

    size_t offset = 0;
    ca_status status = CA_OK;
    while (offset < length) {
        size_t wrote = ca_fs_write(file, text + offset, length - offset, err);
        if (wrote == (size_t)-1 || wrote == 0) {
            status = (err && err->code != CA_OK) ? err->code : CA_IO_ERROR;
            break;
        }
        offset += wrote;
    }

    ca_fs_close(file);
    return status;
}

/* Best-effort parent directory creation, mirroring the C++ implementation. */
static void ensure_parent_dir(const char *path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }

    size_t length = strlen(path);
    size_t separator = length;
    for (size_t i = length; i > 0; --i) {
        if (path[i - 1] == '/' || path[i - 1] == '\\') {
            separator = i - 1;
            break;
        }
    }
    if (separator == length || separator == 0) {
        return;
    }

    cx_string *parent = nullptr;
    cx_status cx = cx_string_init(&parent, nullptr);
    if (cx == CX_OK) {
        cx = cx_string_assign_view(
            parent, cx_string_view_from_parts(path, separator));
    }
    if (cx == CX_OK) {
        ca_error ignored;
        (void)ca_fs_mkdirs(cx_string_cstr(parent), &ignored);
    }
    cx_string_destroy(&parent);
}

/* ------------------------------------------------------------------------- */
/* JSON member helpers                                                       */
/* ------------------------------------------------------------------------- */

static bool json_string_member(const ca_json_val *object, const char *key,
                               const char **text, size_t *length) {
    const ca_json_val *value = ca_json_object_get(object, key);
    size_t len = 0;
    const char *str = ca_json_get_string(value, &len);
    if (str == nullptr) {
        return false;
    }
    *text = str;
    *length = len;
    return true;
}

/* nlohmann's isInteger() accepts both signed and unsigned subtypes. */
static bool json_integer_member(const ca_json_val *object, const char *key,
                                int64_t *out) {
    const ca_json_val *value = ca_json_object_get(object, key);
    switch (ca_json_type_of(value)) {
    case CA_JSON_INT:
        *out = ca_json_get_int(value);
        return true;
    case CA_JSON_UINT:
        *out = (int64_t)ca_json_get_uint(value);
        return true;
    default:
        return false;
    }
}

static bool json_bool_member(const ca_json_val *object, const char *key,
                             bool *out) {
    const ca_json_val *value = ca_json_object_get(object, key);
    switch (ca_json_type_of(value)) {
    case CA_JSON_TRUE:
        *out = true;
        return true;
    case CA_JSON_FALSE:
        *out = false;
        return true;
    default:
        return false;
    }
}

static bool parse_log_level(const char *text, size_t length, int32_t *out) {
    static const struct {
        const char *name;
        int32_t value;
    } names[] = {
        {"trace", 0}, {"debug", 1}, {"info", 2}, {"warn", 3}, {"error", 4},
    };

    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        size_t name_len = strlen(names[i].name);
        if (length == name_len && memcmp(text, names[i].name, length) == 0) {
            *out = names[i].value;
            return true;
        }
    }
    if (length == 1 && text[0] >= '0' && text[0] <= '4') {
        *out = (int32_t)(text[0] - '0');
        return true;
    }
    return false;
}

/* Read a log-level member that may be an integer or a name. */
static bool json_log_level_member(const ca_json_val *object, const char *key,
                                  int32_t *out) {
    int64_t integer = 0;
    if (json_integer_member(object, key, &integer)) {
        *out = (int32_t)integer;
        return true;
    }

    const char *text = nullptr;
    size_t length = 0;
    if (!json_string_member(object, key, &text, &length)) {
        return false;
    }
    return parse_log_level(text, length, out);
}

static bool is_repeat_default(const char *text, size_t length) {
    return (length == 3 && memcmp(text, "off", 3) == 0) ||
           (length == 3 && memcmp(text, "one", 3) == 0) ||
           (length == 3 && memcmp(text, "all", 3) == 0);
}

/* ------------------------------------------------------------------------- */
/* Record lifecycle                                                          */
/* ------------------------------------------------------------------------- */

ca_status ca_config_init(ca_config *config, const cx_allocator *alloc) {
    if (!config) {
        return CA_INVALID_ARGUMENT;
    }
    memset(config, 0, sizeof *config);
    config->log_level = 2;
    config->clear_stops_playback = true;

    ca_status status = new_string(&config->db_path, alloc, "");
    if (status == CA_OK) {
        status = new_string(&config->config_path, alloc, "");
    }
    if (status == CA_OK) {
        status = new_string(&config->device, alloc, "auto");
    }
    if (status == CA_OK) {
        status = new_string(&config->socket_path, alloc, "");
    }
    if (status == CA_OK) {
        status = new_string(&config->repeat_default, alloc, "off");
    }
    if (status != CA_OK) {
        ca_config_destroy(config);
    }
    return status;
}

static ca_status config_assign(ca_config *dst, const ca_config *src) {
    ca_status status = text_assign_view(
        dst->db_path, cx_string_cstr(src->db_path),
        cx_string_length(src->db_path));
    if (status == CA_OK) {
        status = text_assign_view(dst->config_path,
                                  cx_string_cstr(src->config_path),
                                  cx_string_length(src->config_path));
    }
    if (status == CA_OK) {
        status = text_assign_view(dst->device, cx_string_cstr(src->device),
                                  cx_string_length(src->device));
    }
    if (status == CA_OK) {
        status = text_assign_view(dst->socket_path,
                                  cx_string_cstr(src->socket_path),
                                  cx_string_length(src->socket_path));
    }
    if (status == CA_OK) {
        status = text_assign_view(dst->repeat_default,
                                  cx_string_cstr(src->repeat_default),
                                  cx_string_length(src->repeat_default));
    }
    if (status == CA_OK) {
        dst->log_level = src->log_level;
        dst->auto_resume_on_play = src->auto_resume_on_play;
        dst->clear_stops_playback = src->clear_stops_playback;
        dst->autostart = src->autostart;
    }
    return status;
}

ca_status ca_config_copy(ca_config *dst, const ca_config *src,
                         const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);

    ca_status status = ca_config_init(dst, alloc);
    if (status == CA_OK) {
        status = config_assign(dst, src);
    }
    if (status != CA_OK) {
        ca_config_destroy(dst);
    }
    return status;
}

void ca_config_move(ca_config *dst, ca_config *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_config_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_config_destroy(ca_config *config) {
    if (!config) {
        return;
    }
    cx_string_destroy(&config->db_path);
    cx_string_destroy(&config->config_path);
    cx_string_destroy(&config->device);
    cx_string_destroy(&config->socket_path);
    cx_string_destroy(&config->repeat_default);
    memset(config, 0, sizeof *config);
}

/* ------------------------------------------------------------------------- */
/* Load / save                                                               */
/* ------------------------------------------------------------------------- */

static ca_status config_set_default_db_path(ca_config *config,
                                            const cx_allocator *alloc,
                                            ca_error *err) {
    cx_string *value = nullptr;
    ca_status status = ca_paths_default_db_path(&value, alloc, err);
    if (status != CA_OK) {
        return status;
    }
    status = text_assign_view(config->db_path, cx_string_cstr(value),
                              cx_string_length(value));
    cx_string_destroy(&value);
    return status;
}

static ca_status config_set_config_path(ca_config *config, const char *path,
                                        const cx_allocator *alloc,
                                        ca_error *err) {
    if (path != nullptr && path[0] != '\0') {
        return text_assign(config->config_path, path);
    }

    cx_string *value = nullptr;
    ca_status status = ca_paths_default_config_path(&value, alloc, err);
    if (status != CA_OK) {
        return status;
    }
    status = text_assign_view(config->config_path, cx_string_cstr(value),
                              cx_string_length(value));
    cx_string_destroy(&value);
    return status;
}

static ca_status config_apply_json(ca_config *config, const ca_json_val *root) {
    const char *text = nullptr;
    size_t length = 0;

    if (json_string_member(root, "dbPath", &text, &length) && length > 0) {
        ca_status status = text_assign_view(config->db_path, text, length);
        if (status != CA_OK) {
            return status;
        }
    }
    /* configPath is derived from the file being read; ignore any stored value. */
    if (json_string_member(root, "device", &text, &length)) {
        ca_status status = text_assign_view(config->device, text, length);
        if (status != CA_OK) {
            return status;
        }
    }
    (void)json_log_level_member(root, "logLevel", &config->log_level);
    if (json_string_member(root, "socketPath", &text, &length) && length > 0) {
        ca_status status = text_assign_view(config->socket_path, text, length);
        if (status != CA_OK) {
            return status;
        }
    }
    (void)json_bool_member(root, "autoResumeOnPlay",
                           &config->auto_resume_on_play);
    (void)json_bool_member(root, "clearStopsPlayback",
                           &config->clear_stops_playback);
    if (json_string_member(root, "repeatDefault", &text, &length) &&
        is_repeat_default(text, length)) {
        ca_status status =
            text_assign_view(config->repeat_default, text, length);
        if (status != CA_OK) {
            return status;
        }
    }
    (void)json_bool_member(root, "autostart", &config->autostart);

    /* Legacy snake_case keys are applied last, so they win over new keys. */
    if (json_string_member(root, "db_path", &text, &length) && length > 0) {
        ca_status status = text_assign_view(config->db_path, text, length);
        if (status != CA_OK) {
            return status;
        }
    }
    (void)json_log_level_member(root, "log_level", &config->log_level);
    return CA_OK;
}

ca_status ca_config_load(ca_config *out, const char *path,
                         const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!out) {
        return CA_INVALID_ARGUMENT;
    }

    ca_status status = ca_config_init(out, alloc);
    if (status != CA_OK) {
        return status;
    }

    status = config_set_default_db_path(out, alloc, err);
    if (status == CA_OK) {
        status = config_set_config_path(out, path, alloc, err);
    }
    if (status != CA_OK) {
        ca_config_destroy(out);
        return status;
    }

    bool explicit_path = path != nullptr && path[0] != '\0';
    const char *config_file = cx_string_cstr(out->config_path);

    if (!explicit_path && !path_exists(config_file)) {
        /* Older Windows installs kept the config under $HOME/.config. Read it
         * (and later write back there) instead of silently using defaults. */
        cx_string *legacy = nullptr;
        if (ca_paths_legacy_config_path(&legacy, alloc) == CA_OK &&
            cx_string_length(legacy) > 0 &&
            path_exists(cx_string_cstr(legacy))) {
            status = text_assign_view(out->config_path,
                                      cx_string_cstr(legacy),
                                      cx_string_length(legacy));
            cx_string_destroy(&legacy);
            if (status != CA_OK) {
                ca_config_destroy(out);
                return status;
            }
            config_file = cx_string_cstr(out->config_path);
        } else {
            cx_string_destroy(&legacy);
        }
    }

    if (!path_exists(config_file)) {
        return CA_OK;
    }

    cx_string *content = nullptr;
    status = read_text_file(config_file, &content, alloc, err);
    if (status != CA_OK) {
        ca_config_destroy(out);
        return status;
    }
    if (cx_string_length(content) == 0) {
        cx_string_destroy(&content);
        return CA_OK;
    }

    ca_json *document = nullptr;
    status = ca_json_parse(&document, cx_string_cstr(content),
                           cx_string_length(content), alloc, err);
    cx_string_destroy(&content);
    if (status != CA_OK) {
        ca_config_destroy(out);
        if (status == CA_INVALID_ARGUMENT) {
            /* ca_json_parse reports syntax errors as invalid arguments; the
             * config contract reports malformed JSON specifically. */
            if (err) {
                ca_error_set(err, CA_JSON_ERROR, "malformed config JSON");
            }
            return CA_JSON_ERROR;
        }
        return status;
    }

    const ca_json_val *root = ca_json_root(document);
    if (ca_json_type_of(root) != CA_JSON_OBJECT) {
        ca_json_destroy(&document);
        ca_config_destroy(out);
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "config is not an object");
        }
        return CA_INVALID_ARGUMENT;
    }

    status = config_apply_json(out, root);
    ca_json_destroy(&document);
    if (status != CA_OK) {
        ca_config_destroy(out);
        return status;
    }
    return CA_OK;
}

ca_status ca_config_save(const ca_config *config, const cx_allocator *alloc,
                         ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!config || !config->config_path) {
        return CA_INVALID_ARGUMENT;
    }

    const char *config_file = cx_string_cstr(config->config_path);
    ensure_parent_dir(config_file);

    ca_json_writer *writer = nullptr;
    ca_status status = ca_json_writer_init(&writer, 1, alloc, err);
    if (status != CA_OK) {
        return status;
    }

    status = ca_json_writer_begin_object(writer, nullptr, err);
    if (status == CA_OK) {
        status = ca_json_writer_add_cstr(writer, "dbPath",
                                         cx_string_cstr(config->db_path), err);
    }
    if (status == CA_OK) {
        status = ca_json_writer_add_cstr(writer, "device",
                                         cx_string_cstr(config->device), err);
    }
    if (status == CA_OK) {
        status = ca_json_writer_add_int(writer, "logLevel", config->log_level,
                                        err);
    }
    if (status == CA_OK) {
        status = ca_json_writer_add_bool(writer, "autoResumeOnPlay",
                                         config->auto_resume_on_play, err);
    }
    if (status == CA_OK) {
        status = ca_json_writer_add_bool(writer, "clearStopsPlayback",
                                         config->clear_stops_playback, err);
    }
    if (status == CA_OK) {
        status = ca_json_writer_add_cstr(writer, "repeatDefault",
                                         cx_string_cstr(config->repeat_default),
                                         err);
    }
    if (status == CA_OK) {
        status =
            ca_json_writer_add_bool(writer, "autostart", config->autostart, err);
    }
    if (status == CA_OK && cx_string_length(config->socket_path) > 0) {
        status = ca_json_writer_add_cstr(writer, "socketPath",
                                         cx_string_cstr(config->socket_path),
                                         err);
    }
    if (status == CA_OK) {
        status = ca_json_writer_end_object(writer, err);
    }

    cx_string *text = nullptr;
    if (status == CA_OK) {
        status = ca_json_writer_finish(writer, &text, err);
    }
    ca_json_writer_destroy(&writer);
    if (status != CA_OK) {
        cx_string_destroy(&text);
        return status;
    }

    status = write_text_file(config_file, cx_string_cstr(text),
                             cx_string_length(text), err);
    cx_string_destroy(&text);
    return status;
}

/* ------------------------------------------------------------------------- */
/* Raw entry lifecycle                                                       */
/* ------------------------------------------------------------------------- */

ca_status ca_config_entry_init(ca_config_entry *entry,
                               const cx_allocator *alloc) {
    if (!entry) {
        return CA_INVALID_ARGUMENT;
    }
    memset(entry, 0, sizeof *entry);
    ca_status status = ca_from_cx(cx_string_init(&entry->key, alloc));
    if (status == CA_OK) {
        status = ca_from_cx(cx_string_init(&entry->value, alloc));
    }
    if (status != CA_OK) {
        ca_config_entry_destroy(entry);
    }
    return status;
}

ca_status ca_config_entry_copy(ca_config_entry *dst,
                               const ca_config_entry *src,
                               const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    if (src->key != nullptr) {
        ca_status status = ca_from_cx(
            cx_string_from_view(&dst->key, cx_string_view_of(src->key), alloc));
        if (status != CA_OK) {
            return status;
        }
    }
    if (src->value != nullptr) {
        ca_status status = ca_from_cx(cx_string_from_view(
            &dst->value, cx_string_view_of(src->value), alloc));
        if (status != CA_OK) {
            ca_config_entry_destroy(dst);
            return status;
        }
    }
    return CA_OK;
}

void ca_config_entry_destroy(ca_config_entry *entry) {
    if (!entry) {
        return;
    }
    cx_string_destroy(&entry->key);
    cx_string_destroy(&entry->value);
    memset(entry, 0, sizeof *entry);
}

/*
 * cx_vector lifecycle callbacks cannot report allocation failures, so the list
 * builder stashes the allocator and the first failure in thread-local state.
 * This is a private channel; it is not shared with the record copy helpers.
 */
static thread_local const cx_allocator *config_copy_alloc;
static thread_local ca_status config_copy_status;

static void config_copy_reset(const cx_allocator *alloc) {
    config_copy_alloc = alloc ? alloc : cx_allocator_default();
    config_copy_status = CA_OK;
}

static ca_status config_copy_fail(ca_status status) {
    if (status != CA_OK && config_copy_status == CA_OK) {
        config_copy_status = status;
    }
    return status;
}

static void config_entry_copy_cb(void *dst, const void *src) {
    const ca_config_entry *source = (const ca_config_entry *)src;
    ca_config_entry *target = (ca_config_entry *)dst;
    memset(target, 0, sizeof *target);

    if (source->key != nullptr) {
        ca_status status = ca_from_cx(cx_string_from_view(
            &target->key, cx_string_view_of(source->key), config_copy_alloc));
        if (status != CA_OK) {
            (void)config_copy_fail(status);
            return;
        }
    }
    if (source->value != nullptr) {
        ca_status status = ca_from_cx(cx_string_from_view(
            &target->value, cx_string_view_of(source->value),
            config_copy_alloc));
        if (status != CA_OK) {
            (void)config_copy_fail(status);
        }
    }
}

static void config_entry_move_cb(void *dst, void *src) {
    ca_config_entry *target = (ca_config_entry *)dst;
    ca_config_entry *source = (ca_config_entry *)src;
    *target = *source;
    memset(source, 0, sizeof *source);
}

static void config_entry_destroy_cb(void *element) {
    ca_config_entry_destroy((ca_config_entry *)element);
}

static const cx_vector_ops config_entry_ops = {
    .copy = config_entry_copy_cb,
    .move = config_entry_move_cb,
    .destroy = config_entry_destroy_cb,
};

void ca_config_entries_destroy(cx_vector **out) {
    cx_vector_destroy(out);
}

/* ------------------------------------------------------------------------- */
/* Raw access                                                                */
/* ------------------------------------------------------------------------- */

/* Serialize a parsed value: strings unquoted, nullptr as "null", else compact. */
static ca_status value_to_text(const ca_json_val *value, cx_string **out,
                               const cx_allocator *alloc, ca_error *err) {
    *out = nullptr;

    size_t length = 0;
    const char *text = ca_json_get_string(value, &length);
    if (text != nullptr) {
        return ca_from_cx(
            cx_string_from_view(out, cx_string_view_from_parts(text, length),
                                alloc));
    }
    if (ca_json_type_of(value) == CA_JSON_NULL) {
        return ca_from_cx(cx_string_from(out, "null", alloc));
    }

    ca_json_writer *writer = nullptr;
    ca_status status = ca_json_writer_init(&writer, 0, alloc, err);
    if (status != CA_OK) {
        return status;
    }
    status = ca_json_writer_add_value(writer, nullptr, value, err);
    if (status == CA_OK) {
        status = ca_json_writer_finish(writer, out, err);
    }
    ca_json_writer_destroy(&writer);
    return status;
}

/* Read and parse @p path. Returns an empty document list when it is absent. */
static ca_status load_object(const char *path, ca_json **out,
                             const cx_allocator *alloc, ca_error *err) {
    *out = nullptr;
    if (!path_exists(path)) {
        return CA_NOT_FOUND;
    }

    cx_string *content = nullptr;
    ca_status status = read_text_file(path, &content, alloc, err);
    if (status != CA_OK) {
        return status;
    }
    if (cx_string_length(content) == 0) {
        cx_string_destroy(&content);
        return CA_NOT_FOUND;
    }

    status = ca_json_parse(out, cx_string_cstr(content),
                           cx_string_length(content), alloc, err);
    cx_string_destroy(&content);
    if (status != CA_OK) {
        if (status == CA_INVALID_ARGUMENT) {
            if (err) {
                ca_error_set(err, CA_JSON_ERROR, "malformed config JSON");
            }
            return CA_JSON_ERROR;
        }
        return status;
    }
    if (ca_json_type_of(ca_json_root(*out)) != CA_JSON_OBJECT) {
        ca_json_destroy(out);
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "config is not an object");
        }
        return CA_INVALID_ARGUMENT;
    }
    return CA_OK;
}

ca_status ca_config_get_raw(const char *path, const char *key, cx_string **out,
                            const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!out || !key) {
        return CA_INVALID_ARGUMENT;
    }
    *out = nullptr;

    ca_json *document = nullptr;
    ca_status status = load_object(path, &document, alloc, err);
    if (status == CA_NOT_FOUND) {
        if (err) {
            ca_error_set(err, CA_NOT_FOUND,
                         path_exists(path) ? "key not found" : "config not found");
        }
        return CA_NOT_FOUND;
    }
    if (status != CA_OK) {
        return status;
    }

    const ca_json_val *root = ca_json_root(document);
    const ca_json_val *value = ca_json_object_get(root, key);
    if (value == nullptr) {
        ca_json_destroy(&document);
        if (err) {
            ca_error_set(err, CA_NOT_FOUND, "key not found");
        }
        return CA_NOT_FOUND;
    }

    status = value_to_text(value, out, alloc, err);
    ca_json_destroy(&document);
    return status;
}

/*
 * Emit every member of @p root except @p skip, substituting @p replace at the
 * position of @p replace_key (or appending it when absent). This mirrors
 * nlohmann's insertion order: overwriting an existing key keeps its position.
 */
static ca_status write_members(ca_json_writer *writer, const ca_json_val *root,
                               const char *skip, const char *replace_key,
                               const char *replace_text, bool replace_is_json,
                               const ca_json_val *replace_value,
                               ca_error *err) {
    ca_status status = ca_json_writer_begin_object(writer, nullptr, err);
    if (status != CA_OK) {
        return status;
    }

    bool replaced = false;
    size_t count = ca_json_object_size(root);
    for (size_t i = 0; i < count; ++i) {
        const char *member = ca_json_object_key(root, i, nullptr);
        if (member == nullptr) {
            continue;
        }
        if (skip != nullptr && strcmp(member, skip) == 0) {
            continue;
        }
        if (replace_key != nullptr && strcmp(member, replace_key) == 0) {
            replaced = true;
            if (replace_is_json) {
                status = ca_json_writer_add_value(writer, member, replace_value,
                                                  err);
            } else {
                status = ca_json_writer_add_cstr(writer, member, replace_text,
                                                 err);
            }
        } else {
            status = ca_json_writer_add_value(
                writer, member, ca_json_object_get(root, member), err);
        }
        if (status != CA_OK) {
            return status;
        }
    }

    if (replace_key != nullptr && !replaced) {
        if (replace_is_json) {
            status =
                ca_json_writer_add_value(writer, replace_key, replace_value, err);
        } else {
            status =
                ca_json_writer_add_cstr(writer, replace_key, replace_text, err);
        }
        if (status != CA_OK) {
            return status;
        }
    }

    return ca_json_writer_end_object(writer, err);
}

static ca_status write_members_to_file(const char *path,
                                       const ca_json_val *root,
                                       const char *skip,
                                       const char *replace_key,
                                       const char *replace_text,
                                       bool replace_is_json,
                                       const ca_json_val *replace_value,
                                       const cx_allocator *alloc,
                                       ca_error *err) {
    ensure_parent_dir(path);

    ca_json_writer *writer = nullptr;
    ca_status status = ca_json_writer_init(&writer, 1, alloc, err);
    if (status != CA_OK) {
        return status;
    }
    status = write_members(writer, root, skip, replace_key, replace_text,
                           replace_is_json, replace_value, err);
    cx_string *text = nullptr;
    if (status == CA_OK) {
        status = ca_json_writer_finish(writer, &text, err);
    }
    ca_json_writer_destroy(&writer);
    if (status != CA_OK) {
        cx_string_destroy(&text);
        return status;
    }

    status = write_text_file(path, cx_string_cstr(text),
                             cx_string_length(text), err);
    cx_string_destroy(&text);
    return status;
}

/* Parse @p path into an object, or return an empty document when unusable. */
static ca_status load_or_empty(const char *path, ca_json **out,
                               const cx_allocator *alloc, ca_error *err) {
    *out = nullptr;
    ca_json *document = nullptr;
    ca_status status = load_object(path, &document, alloc, err);
    if (status == CA_OK) {
        *out = document;
        return CA_OK;
    }
    if (status == CA_NOT_FOUND) {
        return ca_json_parse(out, "{}", 2, alloc, err);
    }
    if (status == CA_JSON_ERROR || status == CA_INVALID_ARGUMENT) {
        if (err) {
            ca_error_init(err);
        }
        return ca_json_parse(out, "{}", 2, alloc, err);
    }
    return status;
}

ca_status ca_config_set_raw(const char *path, const char *key,
                            const char *value, const cx_allocator *alloc,
                            ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!key) {
        return CA_INVALID_ARGUMENT;
    }
    if (path == nullptr || path[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "cannot write config");
        }
        return CA_IO_ERROR;
    }

    ca_json *document = nullptr;
    ca_status status = load_or_empty(path, &document, alloc, err);
    if (status != CA_OK) {
        return status;
    }

    ca_json *parsed_value = nullptr;
    bool is_json = false;
    const char *text_value = value ? value : "";
    if (value != nullptr && value[0] != '\0') {
        if (ca_json_parse(&parsed_value, value, strlen(value), alloc, nullptr) ==
            CA_OK) {
            is_json = true;
        }
    }

    status = write_members_to_file(
        path, ca_json_root(document), nullptr, key, text_value, is_json,
        is_json ? ca_json_root(parsed_value) : nullptr, alloc, err);

    ca_json_destroy(&parsed_value);
    ca_json_destroy(&document);
    return status;
}

ca_status ca_config_list_raw(const char *path, cx_vector **out,
                             const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!out) {
        return CA_INVALID_ARGUMENT;
    }
    *out = nullptr;

    if (!path_exists(path)) {
        return ca_from_cx(cx_vector_init(out, sizeof(ca_config_entry),
                                         alignof(ca_config_entry),
                                         &config_entry_ops, alloc));
    }

    ca_json *document = nullptr;
    ca_status status = load_object(path, &document, alloc, err);
    if (status == CA_NOT_FOUND || status == CA_INVALID_ARGUMENT) {
        if (err) {
            ca_error_init(err);
        }
        return ca_from_cx(cx_vector_init(out, sizeof(ca_config_entry),
                                         alignof(ca_config_entry),
                                         &config_entry_ops, alloc));
    }
    if (status != CA_OK) {
        return status;
    }

    status = ca_from_cx(cx_vector_init(out, sizeof(ca_config_entry),
                                       alignof(ca_config_entry),
                                       &config_entry_ops, alloc));
    if (status != CA_OK) {
        ca_json_destroy(&document);
        return status;
    }

    const ca_json_val *root = ca_json_root(document);
    size_t count = ca_json_object_size(root);
    for (size_t i = 0; i < count; ++i) {
        size_t key_length = 0;
        const char *key = ca_json_object_key(root, i, &key_length);
        if (key == nullptr || key_length == 0 || strcmp(key, "type") == 0) {
            continue;
        }

        ca_config_entry entry;
        status = ca_config_entry_init(&entry, alloc);
        if (status != CA_OK) {
            break;
        }
        status = text_assign_view(entry.key, key, key_length);
        cx_string *value_text = nullptr;
        if (status == CA_OK) {
            status = value_to_text(ca_json_object_get(root, key), &value_text,
                                   alloc, err);
        }
        if (status == CA_OK) {
            status = text_assign_view(entry.value, cx_string_cstr(value_text),
                                      cx_string_length(value_text));
        }
        cx_string_destroy(&value_text);
        if (status == CA_OK) {
            config_copy_reset(alloc);
            cx_status pushed =
                cx_vector_push_copy(*out, &entry, sizeof entry);
            if (pushed != CX_OK) {
                status = ca_from_cx(pushed);
            } else if (config_copy_status != CA_OK) {
                status = config_copy_status;
            }
        }
        ca_config_entry_destroy(&entry);
        if (status != CA_OK) {
            break;
        }
    }

    ca_json_destroy(&document);
    if (status != CA_OK) {
        cx_vector_destroy(out);
    }
    return status;
}

ca_status ca_config_delete_raw(const char *path, const char *key,
                               const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!key || key[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "empty key");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_json *document = nullptr;
    ca_status status = load_object(path, &document, alloc, err);
    if (status == CA_NOT_FOUND) {
        if (err) {
            ca_error_set(err, CA_NOT_FOUND,
                         path_exists(path) ? "key not found" : "config not found");
        }
        return CA_NOT_FOUND;
    }
    if (status != CA_OK) {
        return status;
    }

    const ca_json_val *root = ca_json_root(document);
    if (ca_json_object_get(root, key) == nullptr) {
        ca_json_destroy(&document);
        if (err) {
            ca_error_set(err, CA_NOT_FOUND, "key not found");
        }
        return CA_NOT_FOUND;
    }

    status = write_members_to_file(path, root, key, nullptr, nullptr, false, nullptr,
                                   alloc, err);
    ca_json_destroy(&document);
    return status;
}

ca_status ca_config_reset_all_raw(const char *path, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (path == nullptr || path[0] == '\0') {
        return CA_OK;
    }
    if (!path_exists(path)) {
        return CA_OK;
    }
    return ca_fs_remove(path, err);
}
