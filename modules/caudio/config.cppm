module;
#include <caudio/config.hpp>

export module caudio.config;

export namespace caudio::config {
using ::caudio::config::Config;
using ::caudio::config::loadConfig;
using ::caudio::config::lockPathFor;
using ::caudio::config::logPathFor;
using ::caudio::config::pidPathFor;
using ::caudio::config::RuntimePaths;
using ::caudio::config::saveConfig;
using ::caudio::config::socketPathFor;
} // namespace caudio::config
