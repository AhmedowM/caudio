module;
#include "service/service_paths.hpp"

export module caudio.service:paths;

export namespace caudio::service::detail {
using ::caudio::service::detail::checkPidAlive;
using ::caudio::service::detail::lockPathForSocket;
using ::caudio::service::detail::pidPathForSocket;
using ::caudio::service::detail::probeSocketAlive;
using ::caudio::service::detail::readPidFile;
using ::caudio::service::detail::releaseLock;
using ::caudio::service::detail::resolveConfigPath;
using ::caudio::service::detail::socketPathForDb;
using ::caudio::service::detail::tryAcquireLock;
} // namespace caudio::service::detail
