module;
#include <caudio/service/ipc_channel.hpp>

export module caudio.service:ipc_channel;

export namespace caudio::service {
using ::caudio::service::deframeMessage;
using ::caudio::service::frameMessage;
using ::caudio::service::IpcChannel;
} // namespace caudio::service
