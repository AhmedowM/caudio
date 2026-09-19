set(CAUDIO_UTILS_SOURCES
  src/utils/utils.cppm
  src/utils/result.cppm
  src/utils/error.cppm
  src/utils/log.cppm
  src/utils/ring.cppm
  src/utils/mpsc_queue.cppm
  src/utils/thread.cppm
  src/utils/version.cppm
)

set(CAUDIO_UTILS_IMPL_SOURCES
  src/utils/error.cpp
  src/utils/log.cpp
  src/utils/thread.cpp
)
caudio_add_component(utils SOURCES ${CAUDIO_UTILS_SOURCES} DEPS Threads::Threads INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(utils PRIVATE ${CAUDIO_UTILS_IMPL_SOURCES})
caudio_add_shared_variant(utils EXTRA_DEPS Threads::Threads)
