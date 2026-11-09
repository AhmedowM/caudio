#ifndef CAUDIO_MEDIA_AUDIO_EXT_H
#define CAUDIO_MEDIA_AUDIO_EXT_H

/**
 * @file caudio/media/audio_ext.h
 * @brief Audio file extension helpers.
 * @ingroup caudio_media
 * @details Shared detection of supported audio file extensions for scanning
 * and path expansion.
 */

#include <caudio/api.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Checks if a path has a supported audio extension.
 *
 * @param path Path to check (UTF-8, NUL-terminated).
 * @return true if extension is a supported audio extension
 * (.mp3/.flac/.ogg/.opus/.oga/.wav/.m4a/.mp4/.wma/.asf/.aiff/.aif/.ape/.mpc/.wv,
 * case-insensitive).
 */
[[nodiscard]] CAUDIO_API bool ca_media_has_audio_ext(const char* path);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_MEDIA_AUDIO_EXT_H */
