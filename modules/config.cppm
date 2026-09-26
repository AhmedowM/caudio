module;
#include <caudio/config.hpp>

export module caudio.ipc:config;

export namespace caudio::config {
using ::caudio::config::Config;
using ::caudio::config::configDeleteRaw;
using ::caudio::config::configGetRaw;
using ::caudio::config::configListRaw;
using ::caudio::config::configResetAllRaw;
using ::caudio::config::configSetRaw;
using ::caudio::config::loadConfig;
using ::caudio::config::lockPathFor;
using ::caudio::config::pidPathFor;
using ::caudio::config::RawConfigValue;
using ::caudio::config::saveConfig;
using ::caudio::config::socketPathFor;
} // namespace caudio::config
