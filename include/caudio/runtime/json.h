#ifndef CAUDIO_RUNTIME_JSON_H
#define CAUDIO_RUNTIME_JSON_H

/**
 * @file caudio/runtime/json.h
 * @brief JSON parsing and serialization wrapper over yyjson.
 *
 * ca_json wraps yyjson, exposing it through caudio's error model and
 * allocator contract. All JSON memory is allocated through the supplied
 * cx_allocator (or the default allocator when NULL).
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>

/* Forward-declare cx_allocator from cstdx/allocator.h; only pointers are used
 * in this header, so the full definition is not needed here. */
typedef struct cx_allocator cx_allocator;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a parsed JSON document.
 *
 * Created with ca_json_parse() or ca_json_parse_file(), released with
 * ca_json_destroy().
 */
typedef struct ca_json ca_json;

/**
 * @brief Opaque handle to a JSON value within a document.
 *
 * Obtained from ca_json_root(), ca_json_array_get(), or
 * ca_json_object_get(). The handle is valid for the lifetime of the owning
 * ca_json handle.
 */
typedef struct ca_json_val ca_json_val;

/**
 * @brief JSON value types.
 */
typedef enum ca_json_type {
    CA_JSON_NULL = 0,
    CA_JSON_TRUE = 1,
    CA_JSON_FALSE = 2,
    CA_JSON_INT = 3,
    CA_JSON_UINT = 4,
    CA_JSON_DOUBLE = 5,
    CA_JSON_STRING = 6,
    CA_JSON_ARRAY = 7,
    CA_JSON_OBJECT = 8,
} ca_json_type;

/**
 * @brief Parse JSON text from memory.
 *
 * @param out   Receives the new ca_json handle on success.
 * @param text  JSON text (UTF-8, null-terminator not required).
 * @param len   Length of text in bytes, or 0 to use strlen(text).
 * @param alloc Allocator for internal memory; NULL means default allocator.
 * @param err   Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT on invalid input,
 *         CA_INVALID_ARGUMENT on malformed JSON, CA_OUT_OF_MEMORY on
 *         allocation failure.
 */
CAUDIO_API ca_status ca_json_parse(ca_json **out, const char *text,
                                   size_t len, const cx_allocator *alloc,
                                   ca_error *err);

/**
 * @brief Parse JSON from a file path.
 *
 * @param path  UTF-8 file path (system-native encoding on Windows).
 * @param alloc Allocator for internal memory; NULL means default allocator.
 * @param err   Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT on invalid input,
 *         CA_IO_ERROR if the file cannot be opened or read,
 *         CA_OUT_OF_MEMORY on allocation failure.
 */
CAUDIO_API ca_status ca_json_parse_file(ca_json **out, const char *path,
                                        const cx_allocator *alloc,
                                        ca_error *err);

/**
 * @brief Destroy a ca_json handle and free all associated memory.
 *
 * Passing NULL or a pointer to NULL is a no-op.
 */
CAUDIO_API void ca_json_destroy(ca_json **out);

/**
 * @brief Return the type of a JSON value.
 */
CAUDIO_API ca_json_type ca_json_type_of(const ca_json_val *val);

/**
 * @brief Get the string value of a JSON string.
 *
 * @param len Receives the byte length of the string (excluding NUL), or NULL.
 * @return Pointer to the string contents, or NULL if val is not a string.
 *         The pointer is valid for the lifetime of the owning ca_json.
 */
CAUDIO_API const char *ca_json_get_string(const ca_json_val *val,
                                          size_t *len);

/**
 * @brief Get the signed integer value of a JSON number.
 *
 * @return The value, or 0 if val is not a number.
 */
CAUDIO_API int64_t ca_json_get_int(const ca_json_val *val);

/**
 * @brief Get the unsigned integer value of a JSON number.
 *
 * @return The value, or 0 if val is not a number.
 */
CAUDIO_API uint64_t ca_json_get_uint(const ca_json_val *val);

/**
 * @brief Get the floating-point value of a JSON number.
 *
 * @return The value, or 0.0 if val is not a number.
 */
CAUDIO_API double ca_json_get_double(const ca_json_val *val);

/**
 * @brief Get the number of elements in a JSON array.
 *
 * @return The element count, or 0 if val is not an array.
 */
CAUDIO_API size_t ca_json_array_size(const ca_json_val *val);

/**
 * @brief Get the element at a given index in a JSON array.
 *
 * @return The element handle, or NULL if val is not an array or idx is
 *         out of bounds.
 */
CAUDIO_API const ca_json_val *ca_json_array_get(const ca_json_val *val,
                                                size_t idx);

/**
 * @brief Get the number of key/value pairs in a JSON object.
 *
 * @return The pair count, or 0 if val is not an object.
 */
CAUDIO_API size_t ca_json_object_size(const ca_json_val *val);

/**
 * @brief Get the value associated with a key in a JSON object.
 *
 * @return The value handle, or NULL if val is not an object or the key
 *         is not found.
 */
CAUDIO_API const ca_json_val *ca_json_object_get(const ca_json_val *val,
                                                 const char *key);

/**
 * @brief Return the root value of a JSON document.
 *
 * @return The root value handle, or NULL if json is NULL.
 */
CAUDIO_API const ca_json_val *ca_json_root(const ca_json *json);

/**
 * @brief Serialize a JSON document to a newly allocated string.
 *
 * The returned string must be freed with the allocator's free function.
 *
 * @param json   The document to serialize.
 * @param pretty If nonzero, produce pretty-printed output (4-space indent).
 * @param len    Receives the byte length of the output (excluding NUL),
 *               or NULL.
 * @param alloc  Allocator for the output string; NULL means default.
 * @param err    Optional error record.
 * @return A newly allocated NUL-terminated string, or NULL on error.
 */
CAUDIO_API char *ca_json_serialize(const ca_json *json, int pretty,
                                   size_t *len, const cx_allocator *alloc,
                                   ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_JSON_H */
