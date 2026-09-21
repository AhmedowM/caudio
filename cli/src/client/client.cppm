module;
#include "cli/client/client.hpp"

export module caudio.client;

export namespace caudio::client {
using ::caudio::client::Client;
using ::caudio::client::Config;
using ::caudio::client::IpcClient;
using ::caudio::client::OutputFormatter;
} // namespace caudio::client
