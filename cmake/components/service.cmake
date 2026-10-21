# service.cmake -- caudio::service (daemon-side command dispatch + IPC server)

set(CAUDIO_SERVICE_MODULE_SOURCES
  modules/caudio/service.cppm
  modules/caudio/service/core.cppm
  modules/caudio/service/ipc_server.cppm
)
set(CAUDIO_SERVICE_SOURCES
  src/service/ipc_server.cpp
  src/service/service_paths.cpp
  src/service/service_status.cpp
  src/service/service_audio.cpp
  src/service/service_impl.cpp
  src/service/dispatch_playback.cpp
  src/service/dispatch_queue.cpp
  src/service/dispatch_library.cpp
  src/service/dispatch_config.cpp
  src/service/tag_writer.cpp
)
caudio_add_component(service SOURCES ${CAUDIO_SERVICE_SOURCES} MODULE_SOURCES ${CAUDIO_SERVICE_MODULE_SOURCES} DEPS caudio::ipc caudio::engine caudio::db caudio::media caudio::player caudio::utils Threads::Threads INCLUDES vendor)
target_link_libraries(service PRIVATE tag)
target_include_directories(service SYSTEM PRIVATE ${CAUDIO_TAGLIB_BUILD_INCLUDES})
if(NOT WIN32)
  find_library(LIBRT rt)
  if(LIBRT)
    target_link_libraries(service PUBLIC ${LIBRT})
  endif()
endif()
ca_set_module_warnings(service)
if(CAUDIO_ENABLE_MODULES)
  target_compile_options(service PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
endif()
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(service EXTRA_DEPS caudio::ipc caudio::engine_shared caudio::db_shared caudio::media_shared caudio::player_shared caudio::utils_shared Threads::Threads)
  if(NOT WIN32)
    find_library(LIBRT rt)
    if(LIBRT)
      target_link_libraries(service_shared PUBLIC ${LIBRT})
    endif()
  endif()
endif()
