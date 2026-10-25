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
using ::caudio::ipc::Json;
using ::caudio::ipc::resultFromJson;
using ::caudio::ipc::serializeReply;
using ::caudio::ipc::serializeRequest;
using ::caudio::ipc::toJson;
using ::caudio::ipc::toJsonString;
} // namespace caudio::ipc
