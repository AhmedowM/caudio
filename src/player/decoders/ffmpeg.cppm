module;
#include "caudio/player/decoders/ffmpeg.hpp"

export module caudio.player:ffmpeg;

export namespace caudio::player {
  using ::caudio::player::TrackMetadata;
  using ::caudio::player::extractMetadata;
  using ::caudio::player::FfmpegDecoder;
}
