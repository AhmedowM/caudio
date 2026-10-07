module;
#include <caudio/client/core.hpp>
#include <caudio/client/ipc_client.hpp>
#include <caudio/client/output_formatter.hpp>

export module caudio.client;

export namespace caudio::client {
using ::caudio::client::Client;
using ::caudio::client::Config;
using ::caudio::client::IpcClient;
using ::caudio::client::OutputFormatter;
} // namespace caudio::client
