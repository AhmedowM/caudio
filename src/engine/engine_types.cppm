module;
#include "caudio/engine/engine_types.hpp"

export module caudio.engine:types;

export namespace caudio::engine {
  using ::caudio::engine::RepeatMode;
  using ::caudio::engine::ShuffleMode;
  using ::caudio::engine::PlaybackState;
  using ::caudio::engine::EngineEventType;
  using ::caudio::engine::EngineEvent;
  using ::caudio::engine::EngineCallbacks;
  using ::caudio::engine::EngineConfig;
  using ::caudio::engine::QueueState;
  using ::caudio::engine::EngineState;
}

