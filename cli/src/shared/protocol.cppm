module;
#include "cli/shared/protocol.hpp"

export module caudio.cli:protocol;

export namespace caudio::cli {
  using ::caudio::cli::ordered_json;
  using ::caudio::cli::IpcRequest;
  using ::caudio::cli::IpcReply;
  using ::caudio::cli::toJson;
  using ::caudio::cli::commandFromJson;
  using ::caudio::cli::fromJson;
  using ::caudio::cli::resultFromJson;
  using ::caudio::cli::serializeRequest;
  using ::caudio::cli::deserializeRequest;
  using ::caudio::cli::serializeReply;
  using ::caudio::cli::deserializeReply;
  using ::caudio::cli::frame;
  using ::caudio::cli::deframe;
  using ::caudio::cli::toJsonString;
}

export namespace caudio::cli::detail {
  using ::caudio::cli::detail::playbackStateToString;
  using ::caudio::cli::detail::playbackStateFromString;
  using ::caudio::cli::detail::repeatModeToString;
  using ::caudio::cli::detail::repeatModeFromString;
  using ::caudio::cli::detail::resultCodeToString;
  using ::caudio::cli::detail::resultCodeFromString;
  using ::caudio::cli::detail::trackToJson;
  using ::caudio::cli::detail::trackFromJson;
  using ::caudio::cli::detail::playlistToJson;
  using ::caudio::cli::detail::playlistFromJson;
  using ::caudio::cli::detail::errorToJson;
  using ::caudio::cli::detail::errorFromJson;
}
