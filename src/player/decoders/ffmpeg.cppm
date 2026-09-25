module;
#include "ffmpeg.hpp"

export module caudio.player:ffmpeg;

export namespace caudio::player {
using ::caudio::player::extractMetadata;
using ::caudio::player::FfmpegDecoder;
using ::caudio::player::TrackMetadata;
} // namespace caudio::player
