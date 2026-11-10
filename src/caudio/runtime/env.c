#include <caudio/runtime/env.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cstdx/allocator.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/**
 * @file caudio/runtime/env.c
 * @brief Process environment access, UTF-8 at the public boundary.
 */

static bool env_name_valid(const char* name) {
    return name != nullptr && name[0] != '\0' && strchr(name, '=') == nullptr;
}

#if defined(_WIN32)
static WCHAR* env_utf8_to_wide(const char* text) {
    const cx_allocator* alloc = cx_allocator_default();
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (len <= 0) {
        return nullptr;
    }

    WCHAR* wide = (WCHAR*)cx_allocator_alloc(alloc, (size_t)len * sizeof(WCHAR), alignof(WCHAR));
    if (wide == nullptr) {
        return nullptr;
    }

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, len) != len) {
        cx_allocator_free(alloc, wide, (size_t)len * sizeof(WCHAR), alignof(WCHAR));
        return nullptr;
    }

    return wide;
}

static ca_status env_lookup(const char* name, char** value, size_t* len, ca_error* err) {
    WCHAR* wide_name = env_utf8_to_wide(name);
    if (wide_name == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid UTF-8 environment variable name");
        }
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator* alloc = cx_allocator_default();
    SetLastError(ERROR_SUCCESS);
    DWORD size = GetEnvironmentVariableW(wide_name, nullptr, 0);
    DWORD query_error = GetLastError();

    if (size == 0) {
        cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));

        if (query_error == ERROR_ENVVAR_NOT_FOUND) {
            return CA_NOT_FOUND;
        }

        char* empty = (char*)cx_allocator_alloc(alloc, 1, 1);
        if (empty == nullptr) {
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate environment value");
            }
            return CA_OUT_OF_MEMORY;
        }
        empty[0] = '\0';
        *value = empty;
        *len = 0;
        return CA_OK;
    }

    WCHAR* wide_value = (WCHAR*)cx_allocator_alloc(alloc, (size_t)size * sizeof(WCHAR), alignof(WCHAR));
    if (wide_value == nullptr) {
        cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate environment value");
        }
        return CA_OUT_OF_MEMORY;
    }

    DWORD written = GetEnvironmentVariableW(wide_name, wide_value, size);
    DWORD read_error = GetLastError();
    cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));

    if (written == 0 && read_error == ERROR_ENVVAR_NOT_FOUND) {
        cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
        return CA_NOT_FOUND;
    }

    if (written >= size) {
        cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "environment value changed during read");
        }
        return CA_IO_ERROR;
    }

    if (written > (DWORD)INT32_MAX) {
        cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_OVERFLOW, "environment value is too large");
        }
        return CA_OVERFLOW;
    }

    int needed =
        WideCharToMultiByte(CP_UTF8, 0, wide_value, (int)written, nullptr, 0, nullptr, nullptr);
    if (needed <= 0 && written > 0) {
        cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "environment value is not valid UTF-16");
        }
        return CA_IO_ERROR;
    }

    char* utf8 = (char*)cx_allocator_alloc(alloc, (size_t)needed + 1, 1);
    if (utf8 == nullptr) {
        cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate environment value");
        }
        return CA_OUT_OF_MEMORY;
    }

    if (needed > 0 && WideCharToMultiByte(CP_UTF8, 0, wide_value, (int)written, utf8, needed,
                                          nullptr, nullptr) != needed) {
        cx_allocator_free(alloc, utf8, (size_t)needed + 1, 1);
        cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "failed to convert environment value to UTF-8");
        }
        return CA_IO_ERROR;
    }

    utf8[needed] = '\0';
    cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));

    *value = utf8;
    *len = (size_t)needed;
    return CA_OK;
}
#else
static ca_status env_lookup(const char* name, char** value, size_t* len, ca_error* err) {
    const char* found = getenv(name);
    if (found == nullptr) {
        return CA_NOT_FOUND;
    }

    size_t found_len = strlen(found);
    const cx_allocator* alloc = cx_allocator_default();
    char* copy = (char*)cx_allocator_alloc(alloc, found_len + 1, 1);
    if (copy == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate environment value");
        }
        return CA_OUT_OF_MEMORY;
    }

    memcpy(copy, found, found_len + 1);
    *value = copy;
    *len = found_len;
    return CA_OK;
}
#endif

ca_status ca_env_get(const char* name, char* out, size_t out_size, size_t* out_len, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out_len) {
        *out_len = 0;
    }

    if (!env_name_valid(name)) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid environment variable name");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (out == nullptr && out_size != 0) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out_size must be 0 when out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (out != nullptr && out_size == 0) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out_size must be nonzero when out is provided");
        }
        return CA_INVALID_ARGUMENT;
    }

    char* value = nullptr;
    size_t len = 0;
    ca_status status = env_lookup(name, &value, &len, err);

    if (status != CA_OK) {
        if (out) {
            out[0] = '\0';
        }
        if (status == CA_NOT_FOUND && err) {
            ca_error_set(err, CA_NOT_FOUND, "environment variable is not set: %s", name);
        }
        return status;
    }

    if (out_len) {
        *out_len = len;
    }

    if (out == nullptr) {
        const cx_allocator* alloc = cx_allocator_default();
        cx_allocator_free(alloc, value, len + 1, 1);
        return CA_OK;
    }

    if (len >= out_size) {
        out[0] = '\0';
        const cx_allocator* alloc = cx_allocator_default();
        cx_allocator_free(alloc, value, len + 1, 1);
        if (err) {
            ca_error_set(err, CA_OVERFLOW, "environment value needs %zu bytes", len + 1);
        }
        return CA_OVERFLOW;
    }

    memcpy(out, value, len + 1);
    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, value, len + 1, 1);
    return CA_OK;
}

ca_status ca_env_set(const char* name, const char* value, bool overwrite, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (!env_name_valid(name) || value == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid environment name or value");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32)
    WCHAR* wide_name = env_utf8_to_wide(name);
    WCHAR* wide_value = env_utf8_to_wide(value);
    if (wide_name == nullptr || wide_value == nullptr) {
        const cx_allocator* alloc = cx_allocator_default();
        cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));
        cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid UTF-8 environment name or value");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (!overwrite) {
        SetLastError(ERROR_SUCCESS);
        DWORD size = GetEnvironmentVariableW(wide_name, nullptr, 0);
        DWORD query_error = GetLastError();

        if (size > 0 || query_error != ERROR_ENVVAR_NOT_FOUND) {
            const cx_allocator* alloc = cx_allocator_default();
            cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));
            cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
            if (err) {
                ca_error_set(err, CA_ALREADY_EXISTS, "environment variable already exists: %s",
                             name);
            }
            return CA_ALREADY_EXISTS;
        }
    }

    if (!SetEnvironmentVariableW(wide_name, wide_value)) {
        DWORD error = GetLastError();
        const cx_allocator* alloc = cx_allocator_default();
        cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));
        cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "SetEnvironmentVariableW failed: %lu",
                         (unsigned long)error);
        }
        return CA_IO_ERROR;
    }

    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));
    cx_allocator_free(alloc, wide_value, 0, alignof(WCHAR));
#else
    if (!overwrite && getenv(name) != nullptr) {
        if (err) {
            ca_error_set(err, CA_ALREADY_EXISTS, "environment variable already exists: %s", name);
        }
        return CA_ALREADY_EXISTS;
    }

    if (setenv(name, value, 1) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "setenv failed: %s", strerror(errno));
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_env_unset(const char* name, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (!env_name_valid(name)) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid environment variable name");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32)
    WCHAR* wide_name = env_utf8_to_wide(name);
    if (wide_name == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid UTF-8 environment variable name");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (!SetEnvironmentVariableW(wide_name, nullptr)) {
        DWORD error = GetLastError();
        const cx_allocator* alloc = cx_allocator_default();
        cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "SetEnvironmentVariableW failed: %lu",
                         (unsigned long)error);
        }
        return CA_IO_ERROR;
    }

    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, wide_name, 0, alignof(WCHAR));
#else
    if (unsetenv(name) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "unsetenv failed: %s", strerror(errno));
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}
