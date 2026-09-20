/**
 * @file ffmpeg.hpp
 * @brief FFmpeg-based audio decoder implementation
 * @ingroup caudio_player
 *
 * This header provides the FfmpegDecoder class which implements IDecoder using
 * FFmpeg libraries (libavcodec, libavformat, libswresample). It supports all
 * common audio formats: OGG/Vorbis, FLAC, MP3, WAV, M4A/AAC, Opus, WMA.
 *
 * Thread Safety: NOT thread-safe. The Player ensures decode() runs on a single
 * decode thread. seek() may be called from the control thread; internal state
 * synchronization uses atomic operations where appropriate.
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wglobal-module"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#pragma GCC diagnostic pop

#include "caudio/utils/utils.hpp"
#include "caudio/player/reader.hpp"
#include "caudio/player/decoders/decoder_interface.hpp"
#include "caudio/player/decoders/decoder_common.hpp"

namespace caudio::player {

struct TrackMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string album_artist;
    std::string genre;
    int year = 0;
    int track_num = 0;
    int disc_num = 0;
    double duration = 0;
    int sample_rate = 0;
    int channels = 0;
    int bitrate = 0;
};

caudio::utils::Expected<TrackMetadata> extractMetadata(std::string_view path);

class FfmpegDecoder final : public IDecoder {
  public:
    static bool probe(std::span<const std::byte> data) noexcept;

    static caudio::utils::Expected<std::unique_ptr<IDecoder>> create(Reader& reader);

    [[nodiscard]] uint32_t sampleRate() const noexcept override;
    [[nodiscard]] uint32_t channels() const noexcept override;
    [[nodiscard]] uint64_t totalFrames() const noexcept override;

    std::size_t decode(std::span<float> out) override;

    caudio::utils::Expected<void> seek(double seconds) override;

    ~FfmpegDecoder() override;

  private:
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
    int flushResampler(std::span<float> out, std::size_t totalDecoded,
                       std::size_t frames) noexcept;

    FfmpegDecoder() = default;

    static int readCallback(void* opaque, uint8_t* buf, int bufSize);

    // AVIO seek: AVSEEK_SIZE queries size, AVSEEK_FORCE stripped, seekable=1
    static int64_t seekCallback(void* opaque, int64_t offset, int whence);

    bool init();

    void cleanup();

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
