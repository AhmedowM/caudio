module;
#include "caudio/engine/history.hpp"

export module caudio.engine:history;

export namespace caudio::engine {
using ::caudio::engine::History;
using ::caudio::engine::HistoryEntry;
} // namespace caudio::engine

export namespace caudio::engine::detail {
using ::caudio::engine::detail::nowMs;
using ::caudio::engine::detail::shouldMarkPlayed;
using ::caudio::engine::detail::shouldMarkPlayedEx;
} // namespace caudio::engine::detail
