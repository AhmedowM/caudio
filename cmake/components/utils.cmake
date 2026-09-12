set(CAUDIO_UTILS_SOURCES
  src/utils/utils.cppm
  src/utils/result.cppm
  src/utils/error.cppm
  src/utils/log.cppm
  src/utils/ring.cppm
  src/utils/mpsc_queue.cppm
  src/utils/thread.cppm
)
caudio_add_component(utils SOURCES ${CAUDIO_UTILS_SOURCES} DEPS Threads::Threads)
caudio_add_shared_variant(utils EXTRA_DEPS Threads::Threads)
