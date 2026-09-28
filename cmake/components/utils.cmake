# utils.cmake -- caudio::utils (no dependencies; everything links this).
set(CAUDIO_UTILS_MODULE_SOURCES
  modules/utils/utils.cppm
  modules/utils/result.cppm
  modules/utils/error.cppm
  modules/utils/log.cppm
  modules/utils/json.cppm
  modules/utils/math.cppm
  modules/utils/ring.cppm
  modules/utils/mpsc_queue.cppm
  modules/utils/print.cppm
  modules/utils/thread.cppm
  modules/utils/version.cppm
)

set(CAUDIO_UTILS_SOURCES
  src/utils/error.cpp
  src/utils/json.cpp
  src/utils/log.cpp
  src/utils/thread.cpp
)
caudio_add_component(utils SOURCES ${CAUDIO_UTILS_SOURCES} MODULE_SOURCES ${CAUDIO_UTILS_MODULE_SOURCES} DEPS Threads::Threads INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include)
# nlohmann is the private backend of src/utils/json.cpp (opaque Json facade):
# plain PRIVATE -I, never a target link (a link would force it into
# install(EXPORT) -- see CMakeLists CAUDIO_NLOHMANN_PRIVATE_INCLUDE).
target_include_directories(utils PRIVATE ${CAUDIO_NLOHMANN_PRIVATE_INCLUDE})
target_include_directories(utils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(utils EXTRA_DEPS Threads::Threads)
  target_include_directories(utils_shared PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
endif()
