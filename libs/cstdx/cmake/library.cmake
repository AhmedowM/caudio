# cstdx library target.
#
# Sources and headers are globbed per directory. CONFIGURE_DEPENDS makes CMake
# re-run configure when files are added or removed, so new implementation files
# are picked up without editing this file.
#
# Note: CMake's documentation generally discourages globbing sources because a
# plain GLOB silently misses new files. CONFIGURE_DEPENDS removes that failure
# mode for the Ninja/Makefile generators used by this project; the trade-off is
# a cheap configure-time timestamp scan on every build.
file(GLOB CSTDX_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.c"
)
file(GLOB CSTDX_PRIVATE_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.h"
)
file(GLOB CSTDX_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/cstdx/*.h"
)

# version.h is generated into the build tree, so it cannot be globbed from the
# source tree. List it explicitly for IDE integration.
list(APPEND CSTDX_PUBLIC_HEADERS
    "${PROJECT_BINARY_DIR}/include/cstdx/version.h"
)

# Co-locate DLLs and executables in the build tree on Windows so tests can find
# shared libraries at runtime without manipulating PATH. Must be set before
# add_library() takes effect.
if(WIN32 AND CSTDX_BUILD_SHARED)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if(CSTDX_BUILD_SHARED)
  add_library(cstdx SHARED
      ${CSTDX_SOURCES} ${CSTDX_PRIVATE_HEADERS} ${CSTDX_PUBLIC_HEADERS})
  target_compile_definitions(cstdx
      PUBLIC  CSTDX_SHARED
      PRIVATE CSTDX_EXPORTS
  )
else()
  add_library(cstdx STATIC
      ${CSTDX_SOURCES} ${CSTDX_PRIVATE_HEADERS} ${CSTDX_PUBLIC_HEADERS})
  target_compile_definitions(cstdx PRIVATE CSTDX_EXPORTS)
endif()

add_library(cstdx::cstdx ALIAS cstdx)

target_include_directories(cstdx
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(cstdx PUBLIC c_std_23)

set_target_properties(cstdx PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
  target_compile_options(cstdx PRIVATE /W4)
else()
  target_compile_options(cstdx PRIVATE -Wall -Wextra -Werror)
endif()
