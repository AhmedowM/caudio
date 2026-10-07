module;
#include <caudio/engine.hpp>
#include <caudio/engine/types.hpp>

export module caudio.engine;

export import :core;

export namespace caudio::engine {
using ::caudio::engine::EngineCallbacks;
using ::caudio::engine::EngineConfig;
using ::caudio::engine::EngineEvent;
using ::caudio::engine::EngineEventType;
using ::caudio::engine::EngineState;
using ::caudio::engine::PlaybackState;
using ::caudio::engine::QueueState;
using ::caudio::engine::RepeatMode;
} // namespace caudio::engine
