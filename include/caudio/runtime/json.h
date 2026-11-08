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

/* Forward-declare cstdx types; only pointers are used in this header, so the
 * full definitions are not needed here. */
typedef struct cx_allocator cx_allocator;
typedef struct cx_string cx_string;

/**
 * @defgroup ca_json_limits JSON parsing limits
 * @{
 */

/**
 * @brief Maximum accepted JSON input size in bytes (16 MiB).
 *
 * Applies to both ca_json_parse() and ca_json_parse_file(). Larger inputs are
 * rejected with CA_OVERFLOW before any parsing work is done.
 */
#define CA_JSON_MAX_BYTES ((size_t)16 * 1024 * 1024)

/**
 * @brief Maximum nesting depth of arrays and objects.
 *
 * A document with more than this many nested arrays/objects is rejected with
 * CA_OVERFLOW. The depth of a scalar document is 0.
 */
#define CA_JSON_MAX_DEPTH 128

/**
 * @brief Maximum number of elements in a single array or key/value pairs in a
 *        single object.
 *
 * Containers with more entries are rejected with CA_OVERFLOW.
 */
#define CA_JSON_MAX_CONTAINER 1000000

/** @} */

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
 * @return CA_OK on success, CA_INVALID_ARGUMENT on invalid input or malformed
 *         JSON, CA_OVERFLOW if the input exceeds CA_JSON_MAX_BYTES or the
 *         document exceeds CA_JSON_MAX_DEPTH or CA_JSON_MAX_CONTAINER,
 *         CA_OUT_OF_MEMORY on allocation failure.
 */
CAUDIO_API ca_status ca_json_parse(ca_json **out, const char *text,
                                   size_t len, const cx_allocator *alloc,
                                   ca_error *err);

/**
 * @brief Parse JSON from a file path.
 *
 * The file is read through ca_fs, so @p path is UTF-8 on every platform,
 * including Windows.
 *
 * @param path  UTF-8 file path.
 * @param alloc Allocator for internal memory; NULL means default allocator.
 * @param err   Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT on invalid input or malformed
 *         JSON, CA_IO_ERROR if the file cannot be opened or read, CA_OVERFLOW
 *         if the file exceeds CA_JSON_MAX_BYTES or the document exceeds
 *         CA_JSON_MAX_DEPTH or CA_JSON_MAX_CONTAINER, CA_OUT_OF_MEMORY on
 *         allocation failure.
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
 * @brief Get the key at @p idx in a JSON object, in document order.
 *
 * @param val Object value to inspect.
 * @param idx Zero-based key index; must be less than ca_json_object_size().
 * @param len Optional; receives the key byte length (excluding NUL).
 * @return The NUL-terminated key, or NULL if @p val is not an object or
 *         @p idx is out of bounds. The pointer is valid for the lifetime of
 *         the owning ca_json.
 */
CAUDIO_API const char *ca_json_object_key(const ca_json_val *val, size_t idx,
                                          size_t *len);

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

/**
 * @brief Opaque incremental JSON writer.
 *
 * Builds one document by streaming values into an internal string builder.
 * Containers are opened and closed explicitly; object members carry a key,
 * array elements and the root value do not. The writer enforces the same
 * depth cap as the parser (CA_JSON_MAX_DEPTH) and produces valid JSON for all
 * inputs.
 *
 * A writer produces exactly one root value. Calling a value function twice at
 * the root, closing an unopened container, or supplying a NULL key for an
 * object member is a CA_INVALID_ARGUMENT error.
 *
 * Create with ca_json_writer_init() and release with
 * ca_json_writer_destroy().
 */
typedef struct ca_json_writer ca_json_writer;

/**
 * @brief Initialize an empty writer.
 *
 * @param out    Receives the new writer on success.
 * @param pretty Nonzero for 2-space pretty printing, zero for compact output.
 * @param alloc  Allocator for the writer and its output; NULL means default.
 * @param err    Optional error record.
 * @return CA_OK, CA_INVALID_ARGUMENT, or CA_OUT_OF_MEMORY.
 */
CAUDIO_API ca_status ca_json_writer_init(ca_json_writer **out, int pretty,
                                         const cx_allocator *alloc,
                                         ca_error *err);

/**
 * @brief Destroy a writer and free its buffer.
 *
 * Passing NULL, or a pointer to NULL, is a no-op.
 */
CAUDIO_API void ca_json_writer_destroy(ca_json_writer **out);

/**
 * @brief Open an object.
 *
 * @param key Member name when the current container is an object; NULL for an
 *            array element or the document root.
 */
CAUDIO_API ca_status ca_json_writer_begin_object(ca_json_writer *writer,
                                                 const char *key,
                                                 ca_error *err);

/** @brief Close the current object. */
CAUDIO_API ca_status ca_json_writer_end_object(ca_json_writer *writer,
                                               ca_error *err);

/**
 * @brief Open an array.
 *
 * @param key Member name when the current container is an object; NULL for an
 *            array element or the document root.
 */
CAUDIO_API ca_status ca_json_writer_begin_array(ca_json_writer *writer,
                                                const char *key,
                                                ca_error *err);

/** @brief Close the current array. */
CAUDIO_API ca_status ca_json_writer_end_array(ca_json_writer *writer,
                                              ca_error *err);

/** @brief Append a JSON null. */
CAUDIO_API ca_status ca_json_writer_add_null(ca_json_writer *writer,
                                             const char *key, ca_error *err);

/** @brief Append a JSON boolean. */
CAUDIO_API ca_status ca_json_writer_add_bool(ca_json_writer *writer,
                                             const char *key, bool value,
                                             ca_error *err);

/** @brief Append a signed integer. */
CAUDIO_API ca_status ca_json_writer_add_int(ca_json_writer *writer,
                                            const char *key, int64_t value,
                                            ca_error *err);

/** @brief Append an unsigned integer. */
CAUDIO_API ca_status ca_json_writer_add_uint(ca_json_writer *writer,
                                             const char *key, uint64_t value,
                                             ca_error *err);

/**
 * @brief Append a number.
 *
 * Non-finite values (NaN, infinities) have no JSON representation and are
 * written as null.
 */
CAUDIO_API ca_status ca_json_writer_add_double(ca_json_writer *writer,
                                               const char *key, double value,
                                               ca_error *err);

/**
 * @brief Append a string.
 *
 * @param value  UTF-8 bytes; may be NULL only when @p length is zero.
 * @param length Byte length of @p value.
 */
CAUDIO_API ca_status ca_json_writer_add_string(ca_json_writer *writer,
                                               const char *key,
                                               const char *value,
                                               size_t length, ca_error *err);

/** @brief Append a NUL-terminated string. NULL is written as "". */
CAUDIO_API ca_status ca_json_writer_add_cstr(ca_json_writer *writer,
                                             const char *key,
                                             const char *value,
                                             ca_error *err);

/**
 * @brief Append an existing parsed value, preserving its JSON shape.
 *
 * Recursively emits @p val (a value obtained from another document) at the
 * writer's current position. Objects keep their document key order; numbers
 * keep their stored subtype. Equivalent to serializing @p val and splicing the
 * result in.
 *
 * @param writer Writer positioned where the value should be emitted.
 * @param key    Object member name, or NULL for array elements and the root.
 * @param val    Value to emit; NULL is written as JSON null.
 * @param err    Optional error record.
 * @return CA_OK, CA_INVALID_ARGUMENT, CA_JSON_ERROR, or CA_OUT_OF_MEMORY.
 */
CAUDIO_API ca_status ca_json_writer_add_value(ca_json_writer *writer,
                                              const char *key,
                                              const ca_json_val *val,
                                              ca_error *err);

/**
 * @brief Finish the document and move its text into @p out.
 *
 * The writer must be at the root with exactly one value written. On success
 * @p out receives an owned string; use cx_string_destroy() to release it. The
 * writer remains usable for a new document only after re-initialization.
 *
 * @return CA_OK, CA_JSON_ERROR when no value or an unclosed container
 *         remains, or CA_OUT_OF_MEMORY.
 */
CAUDIO_API ca_status ca_json_writer_finish(ca_json_writer *writer,
                                           cx_string **out, ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_JSON_H */
