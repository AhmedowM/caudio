module;
#include "caudio/ipc/protocol.hpp"

export module caudio.cli:protocol;

export namespace caudio::cli {
using ::caudio::cli::commandFromJson;
using ::caudio::cli::deframe;
using ::caudio::cli::deserializeReply;
using ::caudio::cli::deserializeRequest;
using ::caudio::cli::frame;
using ::caudio::cli::fromJson;
using ::caudio::cli::IpcReply;
using ::caudio::cli::IpcRequest;
using ::caudio::cli::ordered_json;
using ::caudio::cli::resultFromJson;
using ::caudio::cli::serializeReply;
using ::caudio::cli::serializeRequest;
using ::caudio::cli::toJson;
using ::caudio::cli::toJsonString;
} // namespace caudio::cli

export namespace caudio::cli::detail {
using ::caudio::cli::detail::errorFromJson;
using ::caudio::cli::detail::errorToJson;
using ::caudio::cli::detail::playbackStateFromString;
using ::caudio::cli::detail::playbackStateToString;
using ::caudio::cli::detail::playlistFromJson;
using ::caudio::cli::detail::playlistToJson;
using ::caudio::cli::detail::repeatModeFromString;
using ::caudio::cli::detail::repeatModeToString;
using ::caudio::cli::detail::resultCodeFromString;
using ::caudio::cli::detail::resultCodeToString;
using ::caudio::cli::detail::trackFromJson;
using ::caudio::cli::detail::trackToJson;
} // namespace caudio::cli::detail
