# utils.cmake -- caudio::utils (no dependencies; everything links this).
set(CAUDIO_UTILS_MODULE_SOURCES
  modules/caudio/utils.cppm
  modules/caudio/utils/result.cppm
  modules/caudio/utils/error.cppm
  modules/caudio/utils/log.cppm
  modules/caudio/utils/json.cppm
  modules/caudio/utils/math.cppm
  modules/caudio/utils/ring.cppm
  modules/caudio/utils/print.cppm
  modules/caudio/utils/thread.cppm
  modules/caudio/version.cppm
  modules/caudio/utils/generator.cppm
  modules/caudio/utils/record_json.cppm
)

set(CAUDIO_UTILS_SOURCES
  src/utils/error.cpp
  src/utils/json.cpp
  src/utils/log.cpp
  src/utils/log_sink.cpp
  # Shared Track <-> Json codec (db export/import + ipc wire). It includes the
  # header-only caudio::types records, so no extra link dependency is added.
  src/utils/record_json.cpp
  src/utils/thread.cpp
)
caudio_add_component(utils SOURCES ${CAUDIO_UTILS_SOURCES} MODULE_SOURCES ${CAUDIO_UTILS_MODULE_SOURCES} DEPS Threads::Threads)
ca_set_module_warnings(utils)
target_include_directories(utils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(utils EXTRA_DEPS Threads::Threads)
  if(CAUDIO_BUILD_STATIC)
    target_include_directories(utils_shared PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
  else()
    target_include_directories(utils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
  endif()
endif()
