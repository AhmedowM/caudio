# ipc.cmake -- caudio::ipc (IPC command/result/protocol)

set(CAUDIO_IPC_MODULE_SOURCES
  modules/caudio/ipc.cppm
  modules/caudio/ipc/command.cppm
  modules/caudio/ipc/result.cppm
  modules/caudio/ipc/protocol.cppm
  modules/caudio/ipc/shm_status.cppm
)
set(CAUDIO_IPC_SOURCES
  src/ipc/protocol.cpp
  src/ipc/shm_status.cpp
)
caudio_add_component(ipc SOURCES ${CAUDIO_IPC_SOURCES} MODULE_SOURCES ${CAUDIO_IPC_MODULE_SOURCES} DEPS caudio::utils Threads::Threads INCLUDES vendor)
target_include_directories(ipc SYSTEM PRIVATE ${CAUDIO_NLOHMANN_PRIVATE_INCLUDE})
ca_set_module_warnings(ipc)
if(CAUDIO_ENABLE_MODULES)
  target_compile_options(ipc PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
endif()
target_link_options(ipc PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wl,--allow-multiple-definition>)
