# ca_runtime component — part of caudio's own tree (not a standalone project).
# Depends on cstdx for the cx_allocator contract. Compiles to its own library.

set(_runtime_dir ${PROJECT_SOURCE_DIR}/src/caudio/runtime)

# Globbed with CONFIGURE_DEPENDS: adding an implementation file is enough, and
# CMake re-runs configure to pick it up.
file(GLOB CA_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${_runtime_dir}/*.c"
)
file(GLOB CA_RUNTIME_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/caudio/runtime/*.h"
)

if(CA_RUNTIME_BUILD_SHARED)
  add_library(ca_runtime SHARED ${CA_RUNTIME_SOURCES} ${CA_RUNTIME_PUBLIC_HEADERS})
  target_compile_definitions(ca_runtime
      PUBLIC  CAUDIO_SHARED
      PRIVATE CAUDIO_EXPORTS
  )
else()
  add_library(ca_runtime STATIC ${CA_RUNTIME_SOURCES} ${CA_RUNTIME_PUBLIC_HEADERS})
endif()

add_library(ca_runtime::ca_runtime ALIAS ca_runtime)

target_include_directories(ca_runtime
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(ca_runtime PUBLIC c_std_23)

set_target_properties(ca_runtime PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    SOVERSION ${PROJECT_VERSION_MAJOR}
    VERSION ${PROJECT_VERSION}
)

if(MSVC)
  target_compile_options(ca_runtime PRIVATE /W4)
else()
  target_compile_options(ca_runtime PRIVATE -Wall -Wextra -Werror)
endif()

# cstdx is part of ca_runtime's public API (allocator types cross the boundary),
# so it must be PUBLIC.
target_link_libraries(ca_runtime PUBLIC cstdx::cstdx)

# Feature-test macros must reach every caudio TU before its first system
# header; they stay PRIVATE so the installed package does not export them.
target_compile_definitions(ca_runtime PRIVATE ${CAUDIO_POSIX_DEFINITIONS})

# yyjson is an implementation detail. Fold its OBJECT library into ca_runtime
# directly instead of linking it: a static archive would otherwise record
# yyjson in its exported link interface, and an OBJECT target cannot be
# installed/exported with the package.
target_sources(ca_runtime PRIVATE $<TARGET_OBJECTS:yyjson>)
target_include_directories(ca_runtime PRIVATE
    ${PROJECT_SOURCE_DIR}/libs/third_party/yyjson/src
)

if(WIN32)
  target_link_libraries(ca_runtime PRIVATE ws2_32)
endif()

unset(_runtime_dir)
