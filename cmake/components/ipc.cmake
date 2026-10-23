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
caudio_add_component(ipc SOURCES ${CAUDIO_IPC_SOURCES} MODULE_SOURCES ${CAUDIO_IPC_MODULE_SOURCES} DEPS caudio::config caudio::utils Threads::Threads INCLUDES vendor)
ca_set_module_warnings(ipc)
if(CAUDIO_ENABLE_MODULES)
  target_compile_options(ipc PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
endif()
