module;
#include "caudio/engine/engine_types.hpp"
#include "caudio/engine/history.hpp"
#include "caudio/engine/shuffle.hpp"
#include "caudio/engine/engine.hpp"

export module caudio.engine;

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
  using ::caudio::engine::HistoryEntry;
  using ::caudio::engine::History;
  using ::caudio::engine::SqliteErrGuard;
  using ::caudio::engine::StmtGuard;
  using ::caudio::engine::toString;
  using ::caudio::engine::Engine;
}

export namespace caudio::engine::detail {
  using ::caudio::engine::detail::shufflePerm;
  using ::caudio::engine::detail::nowMs;
  using ::caudio::engine::detail::shouldMarkPlayedEx;
  using ::caudio::engine::detail::shouldMarkPlayed;
}
