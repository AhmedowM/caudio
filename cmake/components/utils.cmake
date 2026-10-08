# utils.cmake -- caudio::utils (no dependencies; everything links this).
set(CAUDIO_UTILS_MODULE_SOURCES
  modules/caudio/utils.cppm
  modules/caudio/utils/result.cppm
  modules/caudio/utils/error.cppm
  modules/caudio/utils/log.cppm
  modules/caudio/utils/json.cppm
  modules/caudio/utils/math.cppm
  modules/caudio/utils/ring.cppm
  modules/caudio/utils/mpsc_queue.cppm
  modules/caudio/utils/print.cppm
  modules/caudio/utils/thread.cppm
  modules/caudio/version.cppm
  modules/caudio/utils/function.cppm
  modules/caudio/utils/generator.cppm
)

set(CAUDIO_UTILS_SOURCES
  src/utils/error.cpp
  src/utils/json.cpp
  src/utils/log.cpp
  src/utils/thread.cpp
)
caudio_add_component(utils SOURCES ${CAUDIO_UTILS_SOURCES} MODULE_SOURCES ${CAUDIO_UTILS_MODULE_SOURCES} DEPS Threads::Threads)
# nlohmann is the private backend of src/utils/json.cpp (opaque Json facade):
# plain PRIVATE -I, never a target link (a link would force it into
# install(EXPORT) -- see CMakeLists CAUDIO_NLOHMANN_PRIVATE_INCLUDE).
target_include_directories(utils SYSTEM PRIVATE ${CAUDIO_NLOHMANN_PRIVATE_INCLUDE})
target_include_directories(utils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(utils EXTRA_DEPS Threads::Threads)
  target_include_directories(utils_shared PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
endif()
