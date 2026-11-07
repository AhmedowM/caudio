# caudio umbrella library.
#
# Single library (caudio.dll / .so / .dylib, or a static archive when
# CAUDIO_BUILD_SHARED=OFF) that links the ca_runtime component and re-exports
# it as one ABI. cstdx is reached transitively through ca_runtime.
# Domain components (ca_db, ca_media, …) are added here as they are built.

if(CAUDIO_BUILD_SHARED)
  add_library(caudio SHARED
      ${PROJECT_SOURCE_DIR}/src/caudio/umbrella.c
  )
  target_compile_definitions(caudio
      PUBLIC  CAUDIO_SHARED
      PRIVATE CAUDIO_EXPORTS
  )
else()
  add_library(caudio STATIC
      ${PROJECT_SOURCE_DIR}/src/caudio/umbrella.c
  )
endif()

target_link_libraries(caudio PUBLIC ca_runtime::ca_runtime)

target_include_directories(caudio
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(caudio PUBLIC c_std_23)

set_target_properties(caudio PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    SOVERSION ${PROJECT_VERSION_MAJOR}
    VERSION ${PROJECT_VERSION}
)

if(MSVC)
  target_compile_options(caudio PRIVATE /W4)
else()
  target_compile_options(caudio PRIVATE -Wall -Wextra -Werror)
endif()

add_library(caudio::caudio ALIAS caudio)
