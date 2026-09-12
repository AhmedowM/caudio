# cli.cmake — caudio::json + caudio::cli_shared + caudio::service + caudio::client

set(CAUDIO_JSON_SOURCES
  src/json/json.cppm
)
caudio_add_component(json SOURCES ${CAUDIO_JSON_SOURCES})
target_include_directories(json PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(json)
caudio_add_shared_variant(json)

set(CAUDIO_CLI_SHARED_SOURCES
  cli/src/cli.cppm
  cli/src/shared/command.cppm
  cli/src/shared/result.cppm
  cli/src/shared/protocol.cppm
  cli/src/config.cppm
)
caudio_add_component(cli_shared SOURCES ${CAUDIO_CLI_SHARED_SOURCES} DEPS caudio::engine caudio::db caudio::utils caudio::json Threads::Threads INCLUDES vendor)
target_include_directories(cli_shared PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(cli_shared)
target_compile_options(cli_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
target_link_options(cli_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

set(CAUDIO_SERVICE_SOURCES
  cli/src/service/service.cppm
  cli/src/service/service_detail.cppm
  cli/src/service/service_impl.cppm
  cli/src/service/shm_status.cppm
  cli/src/service/ipc_channel.cppm
  cli/src/service/ipc_channel_unix.cpp
  cli/src/service/ipc_channel_win.cpp
  cli/src/service/ipc_server.cppm
)
caudio_add_component(service SOURCES ${CAUDIO_SERVICE_SOURCES} DEPS caudio::cli_shared caudio::engine caudio::db caudio::utils Threads::Threads INCLUDES vendor)
target_include_directories(service PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
if(NOT WIN32)
  find_library(LIBRT rt)
  if(LIBRT)
    target_link_libraries(service PUBLIC ${LIBRT})
  endif()
endif()
ca_set_module_warnings(service)
target_compile_options(service PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
caudio_add_shared_variant(service EXTRA_DEPS caudio::cli_shared caudio::engine_shared caudio::db_shared caudio::utils_shared Threads::Threads)
if(NOT WIN32)
  find_library(LIBRT rt)
  if(LIBRT)
    target_link_libraries(service_shared PUBLIC ${LIBRT})
  endif()
endif()

set(CAUDIO_CLIENT_SOURCES
  cli/src/client/client.cppm
  cli/src/client/ipc_client.cppm
  cli/src/client/client_impl.cppm
  cli/src/client/output_formatter.cppm
)
caudio_add_component(client SOURCES ${CAUDIO_CLIENT_SOURCES} DEPS caudio::cli_shared caudio::utils caudio::service Threads::Threads INCLUDES vendor)
target_include_directories(client PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(client)
target_compile_options(client PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
caudio_add_shared_variant(client EXTRA_DEPS caudio::cli_shared caudio::utils_shared caudio::service_shared Threads::Threads)
