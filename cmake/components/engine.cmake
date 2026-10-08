# engine.cmake -- caudio::engine (playback state machine + persistence).
# NOTE: history/shuffle live in src/engine/ (NOT installed), no partitions.
set(CAUDIO_ENGINE_MODULE_SOURCES
  modules/engine.cppm
  modules/engine/types.cppm
  modules/engine/core.cppm
)
set(CAUDIO_ENGINE_SOURCES
  src/engine/engine.cpp
  src/engine/history.cpp
  src/engine/shuffle.cpp
)
caudio_add_component(engine SOURCES ${CAUDIO_ENGINE_SOURCES} MODULE_SOURCES ${CAUDIO_ENGINE_MODULE_SOURCES} DEPS caudio::db caudio::player caudio::utils Threads::Threads)
target_include_directories(engine SYSTEM PRIVATE vendor)
ca_set_module_warnings(engine)

if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(engine EXTRA_DEPS caudio::db_shared caudio::player_shared caudio::utils_shared Threads::Threads)
endif()
