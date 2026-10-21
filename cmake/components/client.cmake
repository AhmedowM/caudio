# client.cmake -- caudio::client (IPC client + human-readable output formatting)

set(CAUDIO_CLIENT_MODULE_SOURCES
  modules/caudio/client.cppm
  modules/caudio/client/core.cppm
  modules/caudio/client/output_formatter.cppm
)
set(CAUDIO_CLIENT_SOURCES
  src/client/ipc_client.cpp
  src/client/client_impl.cpp
  src/client/output_formatter.cpp
)
caudio_add_component(client SOURCES ${CAUDIO_CLIENT_SOURCES} MODULE_SOURCES ${CAUDIO_CLIENT_MODULE_SOURCES} DEPS caudio::ipc caudio::utils Threads::Threads INCLUDES vendor)
ca_set_module_warnings(client)
if(CAUDIO_ENABLE_MODULES)
  target_compile_options(client PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
endif()
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(client EXTRA_DEPS caudio::ipc caudio::utils_shared Threads::Threads)
endif()
