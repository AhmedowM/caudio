#ifndef CAUDIO_MEDIA_DECODER_H
#define CAUDIO_MEDIA_DECODER_H

/**
 * @file caudio/media/decoder.h
 * @brief Audio decoder interface.
 * @ingroup caudio_media
 * @details Provides a decoder interface for audio format decoding.
 * The backend implementation is hidden behind the opaque handle.
 * Mirrors the kanariv::media::Decoder interface in C.
 */

#include <caudio/api.h>
#include <caudio/media/reader.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/log.h>

#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque decoder handle.
 */
typedef struct ca_decoder ca_decoder;

/**
 * @brief Probe raw header bytes for a supported format.
 *
 * @param data Header bytes (up to 32 bytes from the reader start).
 * @return true if a decode attempt should proceed.
 */
[[nodiscard]] CAUDIO_API bool ca_decoder_probe(cx_string_view data);

/**
 * @brief Probe and open a decoder for the given reader.
 *
 * @param[out] out Receives the new decoder handle on success.
 * @param reader Input reader positioned at start of audio data.
 * @param logger Non-owning logger (may be null = silent).
 * @param err Optional error record.
 * @return CA_OK on success, CA_NOT_SUPPORTED if no decoder recognized
 *         the format, CA_IO_ERROR on FFmpeg initialization failure,
 *         CA_INVALID_ARGUMENT on invalid reader seek/tell.
 */
[[nodiscard]] CAUDIO_API ca_status ca_decoder_open(ca_decoder** out,
                                        ca_reader* reader,
                                        ca_logger* logger,
                                        ca_error* err);

/**
 * @brief Get the sample rate of the decoded audio in Hz.
 * @return Native sample rate of the audio stream, set at open.
 */
[[nodiscard]] CAUDIO_API uint32_t ca_decoder_sample_rate(const ca_decoder* self);

/**
 * @brief Get the number of audio channels (1=mono, 2=stereo).
 * @return Channel count; output is always interleaved float samples.
 */
[[nodiscard]] CAUDIO_API uint32_t ca_decoder_channels(const ca_decoder* self);

/**
 * @brief Get total frame count, or 0 if unknown (e.g. live streams).
 * @return Total frames (samples per channel).
 */
[[nodiscard]] CAUDIO_API uint64_t ca_decoder_total_frames(const ca_decoder* self);

/**
 * @brief Decode audio samples into the provided buffer.
 *
 * @param self Decoder handle.
 * @param out Output buffer for interleaved float samples.
 * @return Number of frames decoded (0 = EOF or error).
 * @details Decodes up to out.length / channels() frames.
 */
[[nodiscard]] CAUDIO_API size_t ca_decoder_decode(ca_decoder* self,
                                       float* out,
                                       size_t out_length);

/**
 * @brief Seek to a time position in seconds from stream start.
 *
 * @param self Decoder handle.
 * @param seconds Target time (must be >= 0, finite, not NaN).
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT on invalid seconds,
 *         CA_IO_ERROR on seek failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_decoder_seek(ca_decoder* self,
                                        double seconds,
                                        ca_error* err);

/**
 * @brief Tear down the decoder backend.
 *
 * Passing nullptr is a no-op.
 */
CAUDIO_API void ca_decoder_destroy(ca_decoder* self);

/**
 * @brief Extract metadata tags from an audio file.
 *
 * @param path Filesystem path to probe (UTF-8).
 * @param[out] title Receives title tag (allocated, may be nullptr).
 * @param[out] artist Receives artist tag (allocated, may be nullptr).
 * @param[out] album Receives album tag (allocated, may be nullptr).
 * @param[out] genre Receives genre tag (allocated, may be nullptr).
 * @param[out] duration Receives duration in seconds.
 * @param[out] sample_rate Receives sample rate in Hz.
 * @param[out] channels Receives channel count.
 * @param[out] bitrate Receives bitrate in bps.
 * @param err Optional error record.
 * @return CA_OK on success, ca_status error on failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_decoder_extract_metadata(const char* path,
                                                    cx_string** title,
                                                    cx_string** artist,
                                                    cx_string** album,
                                                    cx_string** genre,
                                                    double* duration,
                                                    uint32_t* sample_rate,
                                                    uint32_t* channels,
                                                    int32_t* bitrate,
                                                    ca_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_MEDIA_DECODER_H */
