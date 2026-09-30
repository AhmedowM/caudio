/**
 * @file ffmpeg_impl.hpp
 * @brief Private FFmpeg backend for caudio::player::Decoder.
 * @ingroup caudio_player
 *
 * Defines Decoder::Impl holding all libav state (libavcodec, libavformat,
 * libswresample). Included only by decoder implementation files, never by
 * public headers, so FFmpeg types do not leak to consumers.
 *
 * Supports all common audio formats: OGG/Vorbis, FLAC, MP3, WAV, M4A/AAC,
 * Opus, WMA. NOT thread-safe; see Decoder docs for the threading contract.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(__GNUC__) && !defined(__clang__)
// -Wglobal-module exists only in GCC; Clang rejects the unknown group.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wglobal-module"
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <caudio/player/decoder.hpp>
#include <caudio/player/reader.hpp>
#include <caudio/utils.hpp>

namespace caudio::player {

struct Decoder::Impl {
    Impl() = default;
    ~Impl();

    [[nodiscard]] uint32_t sampleRate() const noexcept;
    [[nodiscard]] uint32_t channels() const noexcept;
    [[nodiscard]] uint64_t totalFrames() const noexcept;

    std::size_t decode(std::span<float> out);

    caudio::utils::Expected<void> seek(double seconds);

    bool init();

    void cleanup();

    struct PacketDeleter {
        void operator()(AVPacket* p) const noexcept {
            if (p)
                av_packet_free(&p);
        }
    };
    struct FrameDeleter {
        void operator()(AVFrame* p) const noexcept {
            if (p)
                av_frame_free(&p);
        }
    };
    using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
    using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
    struct LayoutGuard {
        AVChannelLayout l{};
        LayoutGuard() = default;
        ~LayoutGuard() {
            av_channel_layout_uninit(&l);
        }
        LayoutGuard(const LayoutGuard&) = delete;
        LayoutGuard& operator=(const LayoutGuard&) = delete;
    };

    int convertFrame(AVFrame* frame, std::span<float> out, std::size_t totalDecoded,
                     std::size_t frames) noexcept;
    int flushResampler(std::span<float> out, std::size_t totalDecoded, std::size_t frames) noexcept;

    static int readCallback(void* opaque, uint8_t* buf, int bufSize);

    // AVIO seek: AVSEEK_SIZE queries size, AVSEEK_FORCE stripped, seekable=1
    static int64_t seekCallback(void* opaque, int64_t offset, int whence);

    Reader* reader_{nullptr};
    AVFormatContext* fmt_{nullptr};
    AVCodecContext* dec_{nullptr};
    AVIOContext* avio_{nullptr};
    struct SwrContext* swr_{nullptr};
    int audioStreamIdx_{-1};
    uint32_t sampleRate_{0};
    uint32_t channels_{0};
    uint64_t totalFrames_{0};
    uint64_t pos_{0};
};

} // namespace caudio::player
