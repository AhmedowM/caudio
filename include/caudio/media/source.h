#ifndef CAUDIO_MEDIA_SOURCE_H
#define CAUDIO_MEDIA_SOURCE_H

/**
 * @file caudio/media/source.h
 * @brief Media source abstractions: paths and native file descriptors.
 * @ingroup caudio_media
 * @details A media source separates where bytes come from from the reader
 * that consumes them. Mirrors the kanariv::media::source interface in C.
 */

#include <caudio/api.h>
#include <caudio/media/reader.h>

#include <caudio/runtime/error.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport mechanism used to read a media source.
 */
typedef enum ca_media_source_kind {
    CA_MEDIA_SOURCE_PATH,      /**< Ordinary filesystem path. */
    CA_MEDIA_SOURCE_DESCRIPTOR /**< Caller-supplied native descriptor. */
} ca_media_source_kind_t;

/**
 * @brief Opaque media source handle.
 */
typedef struct ca_media_source ca_media_source;

/**
 * @brief Source virtual table.
 */
typedef struct ca_media_source_ops {
    /** Returns the transport mechanism used by this source. */
    ca_media_source_kind_t (*kind)(const ca_media_source* self);
    /** Returns a user-facing name for diagnostics. */
    const char* (*display_name)(const ca_media_source* self);
    /** Reports whether readers from this source support seeking. */
    bool (*seekable)(const ca_media_source* self);
    /** Opens a reader for this source. */
    ca_status (*open)(ca_media_source* self, ca_reader** out, ca_error* err);
    /** Destroy the source. */
    void (*destroy)(ca_media_source* self);
} ca_media_source_ops;

struct ca_media_source {
    const ca_media_source_ops* ops;
};

/**
 * @brief Returns the transport mechanism used by this source.
 */
[[nodiscard]] CAUDIO_API ca_media_source_kind_t ca_media_source_kind(const ca_media_source* self);

/**
 * @brief Returns a user-facing name that may be shown in diagnostics.
 */
[[nodiscard]] CAUDIO_API const char* ca_media_source_display_name(const ca_media_source* self);

/**
 * @brief Reports whether readers from this source support seeking.
 */
[[nodiscard]] CAUDIO_API bool ca_media_source_seekable(const ca_media_source* self);

/**
 * @brief Opens a reader for this source.
 *
 * @param[out] out Receives the new reader handle on success.
 * @param err Optional error record.
 * @return CA_OK on success, ca_status error on failure.
 * @details Path sources may be opened repeatedly. Descriptor sources are
 * single-use because ownership moves to the returned reader on success.
 */
[[nodiscard]] CAUDIO_API ca_status ca_media_source_open(ca_media_source* self,
                                             ca_reader** out,
                                             ca_error* err);

/**
 * @brief Destroy a media source.
 *
 * Passing nullptr is a no-op.
 */
CAUDIO_API void ca_media_source_destroy(ca_media_source* self);

/* ---------------------------------------------------------- */
/* Concrete source factories                                  */
/* ---------------------------------------------------------- */

/**
 * @brief Create a path source.
 *
 * @param[out] out Receives the new source handle on success.
 * @param path Filesystem path (UTF-8). An empty path is rejected by open().
 * @param err Optional error record.
 * @return CA_OK on success, CA_OUT_OF_MEMORY on allocation failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_media_source_path(ca_media_source** out,
                                             const char* path,
                                             ca_error* err);

/**
 * @brief Returns the stored filesystem path.
 * @return The path, or nullptr if self is nullptr.
 */
[[nodiscard]] CAUDIO_API const char* ca_media_source_path_path(const ca_media_source* self);

/**
 * @brief Create a descriptor source.
 *
 * @param[out] out Receives the new source handle on success.
 * @param fd Native descriptor.
 * @param readable Caller asserts the descriptor is readable.
 * @param seekable Caller asserts seek() is supported.
 * @param close_on_destroy Transfer ownership to the created reader.
 * @param display_name Optional diagnostic name. Defaults to "descriptor:<fd>".
 * @param err Optional error record.
 * @return CA_OK on success, ca_status error on failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_media_source_descriptor(ca_media_source** out,
                                                    int fd,
                                                    bool readable,
                                                    bool seekable,
                                                    bool close_on_destroy,
                                                    const char* display_name,
                                                    ca_error* err);

/**
 * @brief Reports whether open() has already succeeded (descriptor sources only).
 * @return true if the source has already been opened.
 */
[[nodiscard]] CAUDIO_API bool ca_media_source_consumed(const ca_media_source* self);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_MEDIA_SOURCE_H */
