module;
#include "cli/config.hpp"

export module caudio.cli:config;

export namespace caudio::cli {
using ::caudio::cli::Config;
using ::caudio::cli::configDeleteRaw;
using ::caudio::cli::configGetRaw;
using ::caudio::cli::configListRaw;
using ::caudio::cli::configResetAllRaw;
using ::caudio::cli::configSetRaw;
using ::caudio::cli::loadConfig;
using ::caudio::cli::lockPathFor;
using ::caudio::cli::pidPathFor;
using ::caudio::cli::RawConfigValue;
using ::caudio::cli::saveConfig;
using ::caudio::cli::socketPathFor;
} // namespace caudio::cli
