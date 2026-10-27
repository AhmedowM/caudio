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
caudio_add_component(ipc SOURCES ${CAUDIO_IPC_SOURCES} MODULE_SOURCES ${CAUDIO_IPC_MODULE_SOURCES} DEPS caudio::config caudio::utils Threads::Threads)
ca_set_module_warnings(ipc)
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(ipc EXTRA_DEPS caudio::config_shared caudio::utils_shared Threads::Threads)
endif()
