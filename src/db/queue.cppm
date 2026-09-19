module;
#include "caudio/db/queue.hpp"

export module caudio.db:queue;

export namespace caudio::db {
  using ::caudio::db::queueEnqueueLocked;
  using ::caudio::db::queueDequeueLocked;
  using ::caudio::db::queuePeekLocked;
  using ::caudio::db::queueRemoveLocked;
  using ::caudio::db::queueClearLocked;
  using ::caudio::db::queueListLocked;
  using ::caudio::db::queueCountLocked;
  using ::caudio::db::getQueueLocked;
  using ::caudio::db::listQueuesLocked;
  using ::caudio::db::createQueueLocked;
  using ::caudio::db::deleteQueueLocked;
  using ::caudio::db::setQueueRepeatLocked;
  using ::caudio::db::getQueueItemsLocked;
}
