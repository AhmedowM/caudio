module;
#include <caudio/db/queue.hpp>

export module caudio.db:queue;

export namespace caudio::db {
using ::caudio::db::createQueueLocked;
using ::caudio::db::deleteQueueLocked;
using ::caudio::db::getQueueItemsLocked;
using ::caudio::db::getQueueLocked;
using ::caudio::db::listQueuesLocked;
using ::caudio::db::queueClearLocked;
using ::caudio::db::queueCountLocked;
using ::caudio::db::queueDequeueLocked;
using ::caudio::db::queueEnqueueLocked;
using ::caudio::db::queueListLocked;
using ::caudio::db::queuePeekLocked;
using ::caudio::db::queueRemoveLocked;
using ::caudio::db::setQueueRepeatLocked;
} // namespace caudio::db
