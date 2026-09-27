module;
#include <caudio/ipc/protocol.hpp>

export module caudio.ipc:protocol;

export namespace caudio::ipc {
using ::caudio::ipc::commandFromJson;
using ::caudio::ipc::deframe;
using ::caudio::ipc::deserializeReply;
using ::caudio::ipc::deserializeRequest;
using ::caudio::ipc::frame;
using ::caudio::ipc::fromJson;
using ::caudio::ipc::IpcReply;
using ::caudio::ipc::IpcRequest;
using ::caudio::ipc::ordered_json;
using ::caudio::ipc::resultFromJson;
using ::caudio::ipc::serializeReply;
using ::caudio::ipc::serializeRequest;
using ::caudio::ipc::toJson;
using ::caudio::ipc::toJsonString;
} // namespace caudio::ipc

export namespace caudio::ipc::detail {
using ::caudio::ipc::detail::errorFromJson;
using ::caudio::ipc::detail::errorToJson;
using ::caudio::ipc::detail::playbackStateFromString;
using ::caudio::ipc::detail::playbackStateToString;
using ::caudio::ipc::detail::playlistFromJson;
using ::caudio::ipc::detail::playlistToJson;
using ::caudio::ipc::detail::repeatModeFromString;
using ::caudio::ipc::detail::repeatModeToString;
using ::caudio::ipc::detail::resultCodeFromString;
using ::caudio::ipc::detail::resultCodeToString;
using ::caudio::ipc::detail::trackFromJson;
using ::caudio::ipc::detail::trackToJson;
} // namespace caudio::ipc::detail
