module;
#include <caudio/service/shm_status.hpp>

export module caudio.service:shm_status;

export namespace caudio::service {
using ::caudio::service::atomicLoadDouble;
using ::caudio::service::atomicLoadFloat;
using ::caudio::service::AtomicShmStatus;
using ::caudio::service::atomicStoreDouble;
using ::caudio::service::atomicStoreFloat;
using ::caudio::service::createShmStatus;
using ::caudio::service::ShmStatus;
using ::caudio::service::ShmStatusHandle;
} // namespace caudio::service
