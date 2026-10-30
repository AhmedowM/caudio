# types.cmake -- caudio::types (dependency-free wire + record value types).
# Header-only in headers mode; compiled as a static module carrier when modules
# are enabled so CMake can schedule and export its CXX_MODULES file set.
set(CAUDIO_TYPES_MODULE_SOURCES
  modules/caudio/types.cppm
  modules/caudio/types/wire.cppm
  modules/caudio/types/format.cppm
  modules/caudio/types/parse.cppm
  modules/caudio/types/record.cppm
  modules/caudio/types/db.cppm
  modules/caudio/types/engine.cppm
)
if(CAUDIO_ENABLE_MODULES)
  add_library(types STATIC)
else()
  add_library(types INTERFACE)
endif()
add_library(caudio::types ALIAS types)
set_property(DIRECTORY APPEND PROPERTY CAUDIO_TARGETS types)
if(CAUDIO_ENABLE_MODULES)
  target_include_directories(types PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(types PUBLIC cxx_std_23)
else()
  target_include_directories(types INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(types INTERFACE cxx_std_23)
endif()
if(CAUDIO_ENABLE_MODULES)
  target_sources(types PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules
    FILES ${CAUDIO_TYPES_MODULE_SOURCES})
  ca_set_warnings(types)
  ca_set_module_warnings(types)
  # Install .cppm sources directly; consumers compile them to BMIs or add the
  # install/modules dir to their C++ module search path.
  install(FILES ${CAUDIO_TYPES_MODULE_SOURCES}
    DESTINATION modules/caudio/types COMPONENT dev)
endif()
