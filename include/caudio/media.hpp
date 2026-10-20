#pragma once

/**
 * @file media.hpp
 * @brief Umbrella header for the caudio.media module.
 * @ingroup caudio_media
 * @defgroup caudio_media caudio media
 *
 * Media input module providing:
 * - Reader abstractions (FileReader, MemoryReader) for input sources
 * - FFmpeg-based Decoder for all common formats
 * - Metadata extraction and audio-extension helpers
 */

#include <caudio/media/audio_ext.hpp>
#include <caudio/media/decoder.hpp>
#include <caudio/media/reader.hpp>
