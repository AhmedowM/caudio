# types.cmake -- caudio::types (dependency-free wire + record value types).
# Header-only in all builds (INTERFACE); carries the CXX_MODULES file set
# when modules are enabled. Nothing links it in headers mode, but it is exported
# so `find_package` consumers can link caudio::types for the include dirs.
set(CAUDIO_TYPES_MODULE_SOURCES
  modules/caudio/types.cppm
  modules/caudio/types/wire.cppm
  modules/caudio/types/format.cppm
  modules/caudio/types/parse.cppm
  modules/caudio/types/record.cppm
)
add_library(types INTERFACE)
add_library(caudio::types ALIAS types)
set_property(DIRECTORY APPEND PROPERTY CAUDIO_TARGETS types)
target_include_directories(types INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(types INTERFACE cxx_std_23)
if(CAUDIO_ENABLE_MODULES)
  # Install .cppm sources directly; consumers compile them to BMIs or add the
  # install/modules dir to their C++ module search path.
  install(FILES ${CAUDIO_TYPES_MODULE_SOURCES}
    DESTINATION modules/caudio/types COMPONENT dev)
endif()
