#ifndef CAUDIO_MEDIA_READER_H
#define CAUDIO_MEDIA_READER_H

/**
 * @file caudio/media/reader.h
 * @brief Input reader abstractions for audio data sources.
 * @ingroup caudio_media
 * @details Provides the ca_reader_t abstract interface and concrete
 * implementations for reading audio data from various sources:
 * - ca_reader_file: File-based reading with 64-bit seek support
 * - ca_reader_memory: In-memory buffer reading
 * - ca_reader_descriptor: Native file-descriptor reading
 *
 * Mirrors the kanariv::media::Reader interface in C.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque reader handle.
 */
typedef struct ca_reader ca_reader;

/**
 * @brief Reader virtual table.
 */
typedef struct ca_reader_ops {
    /** Read data into the provided buffer. Returns bytes read (0 = EOF/error). */
    size_t (*read)(ca_reader* self, uint8_t* dst, size_t len);
    /** Seek to a new position. Returns CA_OK on success. */
    ca_status (*seek)(ca_reader* self, int64_t offset, int whence);
    /** Get current read position. Returns -1 on error. */
    int64_t (*tell)(ca_reader* self);
    /** Get total size of the data source. Returns -1 if unknown. */
    int64_t (*size)(ca_reader* self);
    /** Report whether seek() is supported. */
    bool (*seekable)(const ca_reader* self);
    /** Destroy the reader. */
    void (*destroy)(ca_reader* self);
} ca_reader_ops;

struct ca_reader {
    const ca_reader_ops* ops;
};

/**
 * @brief Read data into the provided buffer.
 * @return Number of bytes read (0 = EOF or error).
 */
[[nodiscard]] CAUDIO_API size_t ca_reader_read(ca_reader* self, uint8_t* dst, size_t len);

/**
 * @brief Seek to a new position.
 * @return CA_OK on success, ca_status error on failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_reader_seek(ca_reader* self, int64_t offset, int whence);

/**
 * @brief Get current read position.
 * @return Current byte offset from start, or -1 on error.
 */
[[nodiscard]] CAUDIO_API int64_t ca_reader_tell(ca_reader* self);

/**
 * @brief Get total size of the data source.
 * @return Total size in bytes, or -1 if unknown.
 */
[[nodiscard]] CAUDIO_API int64_t ca_reader_size(ca_reader* self);

/**
 * @brief Report whether seek() is supported by this source.
 * @return true when seek() may succeed; false for streaming sources.
 */
[[nodiscard]] CAUDIO_API bool ca_reader_seekable(const ca_reader* self);

/**
 * @brief Destroy a reader, releasing all resources.
 *
 * Passing nullptr is a no-op.
 */
CAUDIO_API void ca_reader_destroy(ca_reader* self);

/* ---------------------------------------------------------- */
/* Concrete reader factories                                  */
/* ---------------------------------------------------------- */

/**
 * @brief Open a file for reading.
 *
 * Opens the file in binary mode, seeks to end to determine size,
 * then seeks back to start. On Windows, tries wide-char path first
 * then falls back to narrow string.
 *
 * @param[out] out Receives the new reader handle on success.
 * @param path Path to the audio file (UTF-8).
 * @param err Optional error record.
 * @return CA_OK on success, CA_NOT_FOUND if the file cannot be opened,
 *         CA_INVALID_ARGUMENT for an empty path.
 */
[[nodiscard]] CAUDIO_API ca_status ca_reader_file(ca_reader** out,
                                       const char* path,
                                       ca_error* err);

/**
 * @brief Open a memory buffer for reading.
 *
 * Copies the input data to an internal buffer for lifetime safety.
 *
 * @param[out] out Receives the new reader handle on success.
 * @param data Pointer to data (may be nullptr if len is 0).
 * @param len Length in bytes.
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for null data with len>0,
 *         CA_OUT_OF_MEMORY on allocation failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_reader_memory(ca_reader** out,
                                         const uint8_t* data,
                                         size_t len,
                                         ca_error* err);

/**
 * @brief Open a native file descriptor for reading.
 *
 * @param[out] out Receives the new reader handle on success.
 * @param fd Native descriptor (POSIX int or Windows CRT int).
 * @param readable Caller asserts the descriptor is readable.
 * @param seekable Caller asserts seek() is supported.
 * @param close_on_destroy Transfer ownership to the created reader.
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for invalid descriptor,
 *         CA_NOT_SUPPORTED if seekable but position unavailable.
 */
[[nodiscard]] CAUDIO_API ca_status ca_reader_descriptor(ca_reader** out,
                                             int fd,
                                             bool readable,
                                             bool seekable,
                                             bool close_on_destroy,
                                             ca_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_MEDIA_READER_H */
