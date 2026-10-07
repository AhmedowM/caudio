# app.cmake -- caudio::app (application orchestration shared by all frontends:
# daemon lifecycle today; transport policy and command dispatch over time).
# The CLI shell (cli/src/shell/*) stays a thin renderer on top of this lib.
set(CAUDIO_APP_SOURCES
  src/app/lifecycle.cpp
  src/app/transport.cpp
  src/app/playback.cpp
  src/app/format.cpp
  src/app/paths.cpp
  src/app/detail.cpp
  src/app/queue.cpp
  src/app/playlist.cpp
  src/app/library.cpp
  src/app/tags.cpp
  src/app/system.cpp
  src/app/config.cpp
  src/app/preview.cpp
)
caudio_add_component(app SOURCES ${CAUDIO_APP_SOURCES} DEPS caudio::client caudio::service caudio::ipc caudio::db caudio::player caudio::utils Threads::Threads)
ca_set_warnings(app)
if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(app EXTRA_DEPS caudio::client_shared caudio::service_shared caudio::ipc caudio::player_shared Threads::Threads)
endif()
