export module caudio.player;

export import caudio.media;
export import :output;
export import :core;

export namespace caudio::player {
using Reader = caudio::media::Reader;
using FileReader = caudio::media::FileReader;
using MemoryReader = caudio::media::MemoryReader;
using Decoder = caudio::media::Decoder;
using caudio::media::extractMetadata;
using TrackMetadata = caudio::media::TrackMetadata;
} // namespace caudio::player
