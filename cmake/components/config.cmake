# config.cmake -- caudio::config (configuration management: canonical paths,
# load/save; raw key/value helpers live in caudio::config::detail internally).

set(CAUDIO_CONFIG_MODULE_SOURCES
  modules/caudio/config.cppm
)
set(CAUDIO_CONFIG_SOURCES
  src/config.cpp
  src/config/raw.cpp
)
caudio_add_component(config SOURCES ${CAUDIO_CONFIG_SOURCES} MODULE_SOURCES ${CAUDIO_CONFIG_MODULE_SOURCES} DEPS caudio::utils Threads::Threads)
ca_set_module_warnings(config)
