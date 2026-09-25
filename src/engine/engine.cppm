module;
#include <caudio/engine.hpp>
#include <caudio/engine/engine_types.hpp>
#include <caudio/engine/history.hpp>
#include <caudio/engine/shuffle.hpp>

export module caudio.engine;

export namespace caudio::engine {
using ::caudio::engine::Engine;
using ::caudio::engine::EngineCallbacks;
using ::caudio::engine::EngineConfig;
using ::caudio::engine::EngineEvent;
using ::caudio::engine::EngineEventType;
using ::caudio::engine::EngineState;
using ::caudio::engine::History;
using ::caudio::engine::HistoryEntry;
using ::caudio::engine::PlaybackState;
using ::caudio::engine::QueueState;
using ::caudio::engine::RepeatMode;
} // namespace caudio::engine

export namespace caudio::engine::detail {
using ::caudio::engine::detail::nowMs;
using ::caudio::engine::detail::shouldMarkPlayed;
using ::caudio::engine::detail::shufflePerm;
} // namespace caudio::engine::detail
