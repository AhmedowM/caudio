# types.cmake -- caudio::types (dependency-free wire + record value types).
# Header-only in the default build (INTERFACE); carries the CXX_MODULES file set
# when modules are enabled. Nothing links it in headers mode, but it is exported
# so `find_package` consumers can link caudio::types for the include dirs.
set(CAUDIO_TYPES_MODULE_SOURCES
  modules/caudio/types.cppm
  modules/caudio/types/wire.cppm
  modules/caudio/types/record.cppm
)
if(CAUDIO_ENABLE_MODULES)
  caudio_add_component(types MODULE_SOURCES ${CAUDIO_TYPES_MODULE_SOURCES})
else()
  add_library(types INTERFACE)
  add_library(caudio::types ALIAS types)
  target_include_directories(types INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(types INTERFACE cxx_std_23)
endif()
