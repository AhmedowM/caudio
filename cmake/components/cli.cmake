# cli.cmake -- caudio::ipc (IPC command/result/protocol + config) + caudio::service + caudio::client
# The CLI app itself (cli/src/app/*) compiles into the caudio executable only; no cli library.

set(CAUDIO_IPC_MODULE_SOURCES
  modules/ipc/ipc.cppm
  modules/ipc/command.cppm
  modules/ipc/result.cppm
  modules/ipc/protocol.cppm
  modules/config.cppm
)
set(CAUDIO_IPC_SOURCES
  src/ipc/protocol.cpp
  src/config.cpp
)
caudio_add_component(ipc SOURCES ${CAUDIO_IPC_SOURCES} MODULE_SOURCES ${CAUDIO_IPC_MODULE_SOURCES} DEPS caudio::engine caudio::db caudio::utils nlohmann_json::nlohmann_json Threads::Threads INCLUDES vendor)
ca_set_module_warnings(ipc)
if(CAUDIO_ENABLE_MODULES)
  target_compile_options(ipc PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
endif()
target_link_options(ipc PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

set(CAUDIO_SERVICE_MODULE_SOURCES
  modules/service/service.cppm
  modules/service/service_paths.cppm
  modules/service/service_status.cppm
  modules/service/service_audio.cppm
  modules/service/service_impl.cppm
  modules/service/shm_status.cppm
  modules/service/ipc_channel.cppm
  modules/service/ipc_server.cppm
)
set(CAUDIO_SERVICE_SOURCES
  src/service/ipc_channel.cpp
  src/service/ipc_server.cpp
  src/service/shm_status.cpp
  src/service/service_paths.cpp
  src/service/service_status.cpp
  src/service/service_audio.cpp
  src/service/service_impl.cpp
  src/service/dispatch_playback.cpp
  src/service/dispatch_queue.cpp
  src/service/dispatch_library.cpp
  src/service/dispatch_config.cpp
)
caudio_add_component(service SOURCES ${CAUDIO_SERVICE_SOURCES} MODULE_SOURCES ${CAUDIO_SERVICE_MODULE_SOURCES} DEPS caudio::ipc caudio::engine caudio::db caudio::utils Threads::Threads INCLUDES vendor)
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
  caudio_add_shared_variant(service EXTRA_DEPS caudio::ipc caudio::engine_shared caudio::db_shared caudio::utils_shared Threads::Threads)
  if(NOT WIN32)
    find_library(LIBRT rt)
    if(LIBRT)
      target_link_libraries(service_shared PUBLIC ${LIBRT})
    endif()
  endif()
endif()

set(CAUDIO_CLIENT_MODULE_SOURCES
  modules/client/client.cppm
  modules/client/ipc_client.cppm
  modules/client/client_impl.cppm
  modules/client/output_formatter.cppm
)
set(CAUDIO_CLIENT_SOURCES
  src/client/ipc_client.cpp
  src/client/client_impl.cpp
  src/client/output_formatter.cpp
)
caudio_add_component(client SOURCES ${CAUDIO_CLIENT_SOURCES} MODULE_SOURCES ${CAUDIO_CLIENT_MODULE_SOURCES} DEPS caudio::ipc caudio::utils caudio::service Threads::Threads INCLUDES vendor)
ca_set_module_warnings(client)
if(CAUDIO_ENABLE_MODULES)
  target_compile_options(client PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
endif()
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(client EXTRA_DEPS caudio::ipc caudio::utils_shared caudio::service_shared Threads::Threads)
endif()
