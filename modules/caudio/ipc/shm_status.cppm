module;
#include <caudio/ipc/shm_status.hpp>

export module caudio.ipc:shm_status;

export namespace caudio::ipc {
using ::caudio::ipc::atomicLoadDouble;
using ::caudio::ipc::atomicLoadFloat;
using ::caudio::ipc::AtomicShmStatus;
using ::caudio::ipc::atomicStoreDouble;
using ::caudio::ipc::atomicStoreFloat;
using ::caudio::ipc::createShmStatus;
using ::caudio::ipc::ShmStatus;
using ::caudio::ipc::ShmStatusHandle;
using ::caudio::ipc::ShmStatusReader;
using ::caudio::ipc::ShmStatusUpdate;
using ::caudio::ipc::ShmStatusWriter;
} // namespace caudio::ipc
