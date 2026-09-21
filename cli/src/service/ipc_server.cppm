module;
#include "cli/service/ipc_server.hpp"

export module caudio.service:ipc_server;

import :ipc_channel;

export namespace caudio::service {
using ::caudio::service::IpcServer;
#ifndef _WIN32
using ::caudio::service::UnixChannel;
#else
using ::caudio::service::WinPipeChannel;
#endif
} // namespace caudio::service
