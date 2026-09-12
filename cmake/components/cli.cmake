# cli.cmake — caudio::json + caudio::cli_shared + caudio::service + caudio::client

set(CAUDIO_JSON_SOURCES
  src/json/json.cppm
)
caudio_add_component(json SOURCES ${CAUDIO_JSON_SOURCES})
target_include_directories(caudio_json PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(caudio_json)
caudio_add_shared_variant(json)

set(CAUDIO_CLI_SHARED_SOURCES
  cli/src/cli.cppm
  cli/src/shared/command.cppm
  cli/src/shared/result.cppm
  cli/src/shared/protocol.cppm
  cli/src/config.cppm
)
caudio_add_component(cli_shared SOURCES ${CAUDIO_CLI_SHARED_SOURCES} DEPS caudio::engine caudio::db caudio::utils caudio::json Threads::Threads INCLUDES vendor)
target_include_directories(caudio_cli_shared PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(caudio_cli_shared)
target_compile_options(caudio_cli_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
target_link_options(caudio_cli_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

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
target_include_directories(caudio_service PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
if(NOT WIN32)
  find_library(LIBRT rt)
  if(LIBRT)
    target_link_libraries(caudio_service PUBLIC ${LIBRT})
  endif()
endif()
ca_set_module_warnings(caudio_service)
target_compile_options(caudio_service PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
caudio_add_shared_variant(service EXTRA_DEPS caudio::cli_shared caudio::engine_shared caudio::db_shared caudio::utils_shared Threads::Threads)
if(NOT WIN32)
  find_library(LIBRT rt)
  if(LIBRT)
    target_link_libraries(caudio_service_shared PUBLIC ${LIBRT})
  endif()
endif()

set(CAUDIO_CLIENT_SOURCES
  cli/src/client/client.cppm
  cli/src/client/ipc_client.cppm
  cli/src/client/client_impl.cppm
  cli/src/client/output_formatter.cppm
)
caudio_add_component(client SOURCES ${CAUDIO_CLIENT_SOURCES} DEPS caudio::cli_shared caudio::utils caudio::service Threads::Threads INCLUDES vendor)
target_include_directories(caudio_client PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(caudio_client)
target_compile_options(caudio_client PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
caudio_add_shared_variant(client EXTRA_DEPS caudio::cli_shared caudio::utils_shared caudio::service_shared Threads::Threads)
