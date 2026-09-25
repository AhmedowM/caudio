# TODO(Audit Directive 2, Appendix C §2.2-2.3): decouple cli/ kitchen sink — split into
#   components/ipc.cmake    (caudio::ipc     = shared/command+result+protocol, no engine runtime)
#   components/service.cmake(caudio::service = IpcServer/IpcChannel/Service/ShmStatus)
#   components/client.cmake (caudio::client  = IpcClient/Client/OutputFormatter)
# Current targets cli_shared/service/client stay under cli/ for build stability; promote headers
# include/cli/shared/* → include/caudio/ipc/*, include/cli/service/* → include/caudio/service/*,
# include/cli/client/* → include/caudio/client/* with one-release deprecated shims. See AUDIT_REPORT.md §2.2.
# cli.cmake — caudio::cli_shared + caudio::service + caudio::client

set(CAUDIO_CLI_SHARED_MODULE_SOURCES
  src/ipc/cli.cppm
  src/ipc/command.cppm
  src/ipc/result.cppm
  src/ipc/protocol.cppm
  src/config.cppm
)
set(CAUDIO_CLI_SHARED_SOURCES
  src/ipc/protocol.cpp
  src/config.cpp
)
caudio_add_component(cli_shared SOURCES ${CAUDIO_CLI_SHARED_SOURCES} MODULE_SOURCES ${CAUDIO_CLI_SHARED_MODULE_SOURCES} DEPS caudio::engine caudio::db caudio::utils Threads::Threads INCLUDES vendor WITH_FFMPEG)
target_include_directories(cli_shared PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(cli_shared)
target_compile_options(cli_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
target_link_options(cli_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

set(CAUDIO_SERVICE_MODULE_SOURCES
  src/service/service.cppm
  src/service/service_detail.cppm
  src/service/service_impl.cppm
  src/service/shm_status.cppm
  src/service/ipc_channel.cppm
  src/service/ipc_channel_unix.cpp
  src/service/ipc_channel_win.cpp
  src/service/ipc_server.cppm
)
set(CAUDIO_SERVICE_SOURCES
  src/service/ipc_channel.cpp
  src/service/ipc_server.cpp
  src/service/shm_status.cpp
  src/service/service_detail.cpp
  src/service/service_impl.cpp
)
caudio_add_component(service SOURCES ${CAUDIO_SERVICE_SOURCES} MODULE_SOURCES ${CAUDIO_SERVICE_MODULE_SOURCES} DEPS caudio::cli_shared caudio::engine caudio::db caudio::utils Threads::Threads INCLUDES vendor WITH_FFMPEG)
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

set(CAUDIO_CLIENT_MODULE_SOURCES
  src/client/client.cppm
  src/client/ipc_client.cppm
  src/client/client_impl.cppm
  src/client/output_formatter.cppm
)
set(CAUDIO_CLIENT_SOURCES
  src/client/ipc_client.cpp
  src/client/client_impl.cpp
  src/client/output_formatter.cpp
)
caudio_add_component(client SOURCES ${CAUDIO_CLIENT_SOURCES} MODULE_SOURCES ${CAUDIO_CLIENT_MODULE_SOURCES} DEPS caudio::cli_shared caudio::utils caudio::service Threads::Threads INCLUDES vendor WITH_FFMPEG)
target_include_directories(client PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(client)
target_compile_options(client PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
caudio_add_shared_variant(client EXTRA_DEPS caudio::cli_shared caudio::utils_shared caudio::service_shared Threads::Threads)
