# service.cmake -- caudio::service (daemon-side command dispatch + IPC server)

set(CAUDIO_SERVICE_MODULE_SOURCES
  modules/caudio/service.cppm
  modules/caudio/service/core.cppm
)
set(CAUDIO_SERVICE_SOURCES
  src/service/ipc_server.cpp
  src/service/service_paths.cpp
  src/service/service_status.cpp
  src/service/service_impl.cpp
  src/service/dispatch_playback.cpp
  src/service/dispatch_queue.cpp
  src/service/dispatch_library.cpp
  src/service/dispatch_config.cpp
  src/service/tag_writer.cpp
)
caudio_add_component(service SOURCES ${CAUDIO_SERVICE_SOURCES} MODULE_SOURCES ${CAUDIO_SERVICE_MODULE_SOURCES} DEPS caudio::config caudio::ipc caudio::utils Threads::Threads INCLUDES vendor PRIVATE_DEPS caudio::engine caudio::db caudio::media caudio::player)
target_link_libraries(service PRIVATE tag)
target_include_directories(service SYSTEM PRIVATE ${CAUDIO_TAGLIB_BUILD_INCLUDES})
# Link rt by name PRIVATE: a find_library() absolute path would be copied into
# the installed caudioTargets.cmake and break relocation. glibc >= 2.34 has the
# shm/clock symbols in libc; older Linux still needs -lrt, which stays a
# link-only (relocatable) name in the export.
function(_caudio_service_link_rt tgt)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${tgt} PRIVATE rt)
  endif()
endfunction()
_caudio_service_link_rt(service)
ca_set_module_warnings(service)
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(service EXTRA_DEPS caudio::ipc caudio::engine_shared caudio::db_shared caudio::media_shared caudio::player_shared caudio::utils_shared Threads::Threads)
  _caudio_service_link_rt(service_shared)
endif()
