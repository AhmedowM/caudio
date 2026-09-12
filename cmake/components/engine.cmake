set(CAUDIO_ENGINE_SOURCES
  src/engine/engine.cppm
  src/engine/engine_types.cppm
  src/engine/history.cppm
  src/engine/shuffle.cppm
)
caudio_add_component(engine SOURCES ${CAUDIO_ENGINE_SOURCES} DEPS caudio::db caudio::player caudio::utils Threads::Threads)
target_include_directories(engine PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
ca_set_module_warnings(engine)

caudio_add_shared_variant(engine EXTRA_DEPS caudio::db_shared caudio::player_shared caudio::utils_shared Threads::Threads)
