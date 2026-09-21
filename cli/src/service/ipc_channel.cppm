module;
#include "cli/service/ipc_channel.hpp"

export module caudio.service:ipc_channel;

export namespace caudio::service {
  using ::caudio::service::IpcChannel;
  using ::caudio::service::socketPathFor;
  using ::caudio::service::frameMessage;
  using ::caudio::service::deframeMessage;
}
