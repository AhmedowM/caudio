module;
#include <caudio/player/output.hpp>

export module caudio.player:output;

export namespace caudio::player {
using ::caudio::player::AudioOutput;
using ::caudio::player::enumerateDevices;
} // namespace caudio::player
