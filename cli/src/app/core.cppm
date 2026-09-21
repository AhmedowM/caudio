module;
#include "cli/app/core.hpp"

export module caudio.app:core;

export namespace caudio::app {
using ::caudio::app::App;
using ::caudio::app::detail::parseDuration;
using ::caudio::app::detail::parseSeek;
using ::caudio::app::detail::parseTime;
using ::caudio::app::detail::parseVolume;
using ::caudio::app::detail::writePlaylistJson;
using ::caudio::app::detail::writePlaylistText;
} // namespace caudio::app

export namespace caudio::app::detail {
using ::caudio::app::detail::parseDuration;
using ::caudio::app::detail::parseSeek;
using ::caudio::app::detail::parseTime;
using ::caudio::app::detail::parseVolume;
using ::caudio::app::detail::writePlaylistJson;
using ::caudio::app::detail::writePlaylistText;
} // namespace caudio::app::detail
