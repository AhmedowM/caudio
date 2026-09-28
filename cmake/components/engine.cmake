set(CAUDIO_ENGINE_MODULE_SOURCES
  modules/engine/engine.cppm
  modules/engine/engine_types.cppm
  modules/engine/history.cppm
  modules/engine/shuffle.cppm
)
set(CAUDIO_ENGINE_SOURCES
  src/engine/engine.cpp
  src/engine/history.cpp
  src/engine/shuffle.cpp
)
caudio_add_component(engine SOURCES ${CAUDIO_ENGINE_SOURCES} MODULE_SOURCES ${CAUDIO_ENGINE_MODULE_SOURCES} DEPS caudio::db caudio::player caudio::utils Threads::Threads)
target_include_directories(engine PRIVATE vendor)
ca_set_module_warnings(engine)

if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(engine EXTRA_DEPS caudio::db_shared caudio::player_shared caudio::utils_shared Threads::Threads)
endif()
