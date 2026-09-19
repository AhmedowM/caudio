module;
#include "caudio/player/output.hpp"

export module caudio.player:output;

export namespace caudio::player {
  using ::caudio::player::DeviceInfo;
  using ::caudio::player::DeviceList;
  using ::caudio::player::enumerateDevices;
  using ::caudio::player::AudioOutput;
}
