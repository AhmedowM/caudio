cmake_minimum_required(VERSION 3.28)

include(cmake/version.cmake)

project(caudio VERSION ${PROJECT_VERSION} LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Co-locate DLLs and executables in the build tree so tests can find shared
# libraries at runtime without manipulating PATH.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(CAUDIO_BUILD_TESTS "Build unit tests" ON)
option(CAUDIO_BUILD_SHARED "Build caudio as a shared library" ON)
option(CA_RUNTIME_BUILD_SHARED "Build ca_runtime as a shared library" ON)

if(CAUDIO_BUILD_TESTS)
  enable_testing()
  # cstdx is added as a subdirectory, so its own default (top-level only)
  # would disable its tests. Propagate the parent's choice instead.
  set(CSTDX_BUILD_TESTS ON)
endif()

option(CAUDIO_USE_SYSTEM_CSTDX "Use an installed cstdx instead of the in-tree copy" OFF)
if(CAUDIO_USE_SYSTEM_CSTDX)
  find_package(cstdx CONFIG REQUIRED)
else()
  add_subdirectory(libs/cstdx)
endif()

# ca_runtime lives inside caudio's own tree (include/caudio/runtime, src/caudio/runtime).
# It is self-contained: no dependency on cstdx.
add_subdirectory(src/caudio/runtime)

if(CAUDIO_BUILD_TESTS)
  add_subdirectory(tests)
endif()

option(CAUDIO_ENABLE_CCACHE "use ccache/sccache compiler launcher if found (opt-in)" ON)

if(CAUDIO_ENABLE_CCACHE)
  foreach(_launcher ccache sccache)
    find_program(_CAUDIO_LAUNCHER ${_launcher})
    if(_CAUDIO_LAUNCHER)
      set(CMAKE_C_COMPILER_LAUNCHER ${_CAUDIO_LAUNCHER})
      message(STATUS "Compiler launcher: ${_CAUDIO_LAUNCHER}")
      break()
    endif()
  endforeach()
  if(NOT CMAKE_C_COMPILER_LAUNCHER)
    message(WARNING "CAUDIO_ENABLE_CCACHE=ON but neither ccache nor sccache found -- compiling without launcher")
  endif()
endif()
unset(_launcher)

# ---------------------------------------------------------------------------
# caudio umbrella library.
#
# Single shared library (caudio.dll / .so / .dylib) that links the ca_runtime
# component and re-exports it as one ABI. The umbrella deliberately does NOT
# link cstdx — cstdx is a separate, independently consumable shared library.
# Domain components (ca_db, ca_media, …) are added here as they are built.
# ---------------------------------------------------------------------------

add_library(caudio SHARED
    src/caudio/umbrella.c
)

target_link_libraries(caudio PUBLIC ca_runtime::ca_runtime)

target_include_directories(caudio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(caudio PUBLIC c_std_23)

set_target_properties(caudio PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    SOVERSION ${PROJECT_VERSION_MAJOR}
    VERSION ${PROJECT_VERSION}
)

if(MSVC)
  target_compile_options(caudio PRIVATE /W4)
else()
  target_compile_options(caudio PRIVATE -Wall -Wextra -Werror)
endif()

add_library(caudio::caudio ALIAS caudio)

# ---------------------------------------------------------------------------
# Install and export
# ---------------------------------------------------------------------------

include(CMakePackageConfigHelpers)

install(TARGETS caudio ca_runtime
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT caudioTargets
    FILE caudioTargets.cmake
    NAMESPACE caudio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio
)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/caudioConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/caudioConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/caudioConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/caudioConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/caudioConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio
)
