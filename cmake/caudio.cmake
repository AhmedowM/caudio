# caudio umbrella library.
#
# Single library (caudio.dll / .so / .dylib, or a static archive when
# CAUDIO_BUILD_SHARED=OFF) that links the ca_runtime component and re-exports
# it as one ABI. cstdx is reached transitively through ca_runtime.
# Domain components (ca_db, ca_media, …) are added here as they are built.

# Domain layers compiled directly into the umbrella. Globbed with
# CONFIGURE_DEPENDS so adding an implementation file is enough.
file(GLOB _caudio_domain_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/caudio/types/*.c"
    "${PROJECT_SOURCE_DIR}/src/caudio/config/*.c"
    "${PROJECT_SOURCE_DIR}/src/caudio/utils/*.c"
    "${PROJECT_SOURCE_DIR}/src/caudio/media/*.c"
    "${PROJECT_SOURCE_DIR}/src/caudio/player/*.c"
)
file(GLOB _caudio_domain_headers CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/caudio/player/*.h"
)
# Vendored BLAKE3 (see libs/third_party/README.md for provenance). Built STATIC
# so the umbrella has exactly one copy; portable-only build per kanariv.
file(GLOB _caudio_vendor_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/libs/third_party/blake3.c"
    "${PROJECT_SOURCE_DIR}/libs/third_party/blake3_portable.c"
)

if(CAUDIO_BUILD_SHARED)
  add_library(caudio SHARED
      ${PROJECT_SOURCE_DIR}/src/caudio/umbrella.c
      ${_caudio_domain_sources}
      ${_caudio_vendor_sources}
      ${_caudio_domain_headers}
  )
  target_compile_definitions(caudio
      PUBLIC  CAUDIO_SHARED
      PRIVATE CAUDIO_EXPORTS
  )
else()
  add_library(caudio STATIC
      ${PROJECT_SOURCE_DIR}/src/caudio/umbrella.c
      ${_caudio_domain_sources}
      ${_caudio_vendor_sources}
      ${_caudio_domain_headers}
  )
endif()

unset(_caudio_domain_sources)
unset(_caudio_domain_headers)
unset(_caudio_vendor_sources)

# Vendored BLAKE3 compiles with relaxed warnings: upstream blake3.c has unused
# variable warnings (e.g. num_cvs) under -Werror on some toolchains/macros.
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/libs/third_party/blake3.c
    ${PROJECT_SOURCE_DIR}/libs/third_party/blake3_portable.c
    PROPERTIES
    COMPILE_FLAGS "-Wno-unused-variable -Wno-unused-but-set-variable"
)

target_link_libraries(caudio PUBLIC ca_runtime::ca_runtime)

# Feature-test macros must reach every caudio TU before its first system
# header; they stay PRIVATE so the installed package does not export them.
target_compile_definitions(caudio PRIVATE ${CAUDIO_POSIX_DEFINITIONS})

target_include_directories(caudio
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/third_party>
)

target_compile_features(caudio PUBLIC c_std_23)

set_target_properties(caudio PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    SOVERSION ${PROJECT_VERSION_MAJOR}
    VERSION ${PROJECT_VERSION}
)

# BLAKE3 is built portable-only (matches kanariv): no AVX512/AVX2/SSE41/SSE2/NEON.
target_compile_definitions(caudio PRIVATE
    BLAKE3_NO_AVX512 BLAKE3_NO_AVX2 BLAKE3_NO_SSE41 BLAKE3_NO_SSE2 BLAKE3_USE_NEON=0
)

# FFmpeg setup: system FFmpeg first, then a trimmed build produced by
# scripts/build-ffmpeg.py (consumed via KANARIV_TRIMMED_ROOT / FFmpeg_ROOT).
# Map KANARIV_TRIMMED_ROOT onto FFmpeg_ROOT so FindFFmpeg consumes it.
if(KANARIV_TRIMMED_ROOT AND NOT FFmpeg_ROOT)
  set(FFmpeg_ROOT "${KANARIV_TRIMMED_ROOT}")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindFFmpeg.cmake)

if(NOT FFmpeg_FOUND)
  include(${CMAKE_CURRENT_LIST_DIR}/SetupFFmpeg.cmake)
  kanariv_setup_ffmpeg()
endif()

if(FFmpeg_FOUND)
  target_link_libraries(caudio PRIVATE
      FFmpeg::avcodec
      FFmpeg::avformat
      FFmpeg::avutil
      FFmpeg::swresample)
  target_include_directories(caudio PRIVATE
      ${FFmpeg_AVCODEC_INCLUDE_DIR}
      ${FFmpeg_AVFORMAT_INCLUDE_DIR}
      ${FFmpeg_AVUTIL_INCLUDE_DIR}
      ${FFmpeg_SWRESAMPLE_INCLUDE_DIR})
endif()

if(MSVC)
  target_compile_options(caudio PRIVATE /W4)
else()
  target_compile_options(caudio PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(caudio PRIVATE m)
endif()

add_library(caudio::caudio ALIAS caudio)
