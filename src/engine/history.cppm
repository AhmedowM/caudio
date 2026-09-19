module;
#include "caudio/engine/history.hpp"

export module caudio.engine:history;

export namespace caudio::engine {
  using ::caudio::engine::HistoryEntry;
  using ::caudio::engine::History;
}

export namespace caudio::engine::detail {
  using ::caudio::engine::detail::nowMs;
  using ::caudio::engine::detail::shouldMarkPlayedEx;
  using ::caudio::engine::detail::shouldMarkPlayed;
}
