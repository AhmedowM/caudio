# FindFFmpeg.cmake — locates FFmpeg shared build
# Provides imported targets FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil etc.
# Search order: CMAKE_PREFIX_PATH / FFmpeg_ROOT (var or env) / auto-detected from ffmpeg executable / system paths
# No hardcoded absolute paths — set -DFFmpeg_ROOT=/path/to/ffmpeg or add ffmpeg/bin to PATH

# Auto-detect FFmpeg root from ffmpeg executable in PATH (portable, no hardcoding)
if(NOT FFmpeg_ROOT AND NOT DEFINED ENV{FFmpeg_ROOT})
  find_program(_FFmpeg_EXECUTABLE_HINT ffmpeg ffprobe
    PATHS ENV PATH
    NO_DEFAULT_PATH
  )
  # Fallback to default find_program search
  if(NOT _FFmpeg_EXECUTABLE_HINT)
    find_program(_FFmpeg_EXECUTABLE_HINT ffmpeg)
  endif()
  if(_FFmpeg_EXECUTABLE_HINT)
    get_filename_component(_FFmpeg_BIN_HINT "${_FFmpeg_EXECUTABLE_HINT}" DIRECTORY)
    get_filename_component(_FFmpeg_ROOT_HINT "${_FFmpeg_BIN_HINT}/.." ABSOLUTE)
    list(APPEND _FFmpeg_SEARCH_HINTS "${_FFmpeg_ROOT_HINT}")
  endif()
  # Also check common user-local install via USERPROFILE (still env-based, not hardcoded user)
  if(DEFINED ENV{USERPROFILE})
    list(APPEND _FFmpeg_SEARCH_HINTS "$ENV{USERPROFILE}/ffmpeg")
  endif()
  if(DEFINED ENV{HOME})
    list(APPEND _FFmpeg_SEARCH_HINTS "$ENV{HOME}/ffmpeg")
  endif()
endif()

find_path(FFmpeg_AVCODEC_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT ${_FFmpeg_SEARCH_HINTS}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH
)
find_path(FFmpeg_AVCODEC_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  PATH_SUFFIXES include
)

find_library(FFmpeg_AVCODEC_LIBRARY
  NAMES avcodec libavcodec
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT ${_FFmpeg_SEARCH_HINTS}
  PATH_SUFFIXES lib bin
)

find_library(FFmpeg_AVFORMAT_LIBRARY
  NAMES avformat libavformat
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT ${_FFmpeg_SEARCH_HINTS}
  PATH_SUFFIXES lib bin
)

find_library(FFmpeg_AVUTIL_LIBRARY
  NAMES avutil libavutil
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT ${_FFmpeg_SEARCH_HINTS}
  PATH_SUFFIXES lib bin
)

find_library(FFmpeg_SWRESAMPLE_LIBRARY
  NAMES swresample libswresample
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT ${_FFmpeg_SEARCH_HINTS}
  PATH_SUFFIXES lib bin
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
  DEFAULT_MSG
  FFmpeg_AVCODEC_LIBRARY
  FFmpeg_AVCODEC_INCLUDE_DIR
  FFmpeg_AVFORMAT_LIBRARY
  FFmpeg_AVUTIL_LIBRARY
)

if(FFmpeg_FOUND)
  set(FFmpeg_INCLUDE_DIRS ${FFmpeg_AVCODEC_INCLUDE_DIR})
  set(FFmpeg_LIBRARIES ${FFmpeg_AVCODEC_LIBRARY} ${FFmpeg_AVFORMAT_LIBRARY} ${FFmpeg_AVUTIL_LIBRARY})
  if(FFmpeg_SWRESAMPLE_LIBRARY)
    list(APPEND FFmpeg_LIBRARIES ${FFmpeg_SWRESAMPLE_LIBRARY})
  endif()

  if(NOT TARGET FFmpeg::avcodec)
    add_library(FFmpeg::avcodec UNKNOWN IMPORTED)
    set_target_properties(FFmpeg::avcodec PROPERTIES
      IMPORTED_LOCATION "${FFmpeg_AVCODEC_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_AVCODEC_INCLUDE_DIR}"
    )
  endif()
  if(NOT TARGET FFmpeg::avformat)
    add_library(FFmpeg::avformat UNKNOWN IMPORTED)
    set_target_properties(FFmpeg::avformat PROPERTIES
      IMPORTED_LOCATION "${FFmpeg_AVFORMAT_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_AVCODEC_INCLUDE_DIR}"
    )
  endif()
  if(NOT TARGET FFmpeg::avutil)
    add_library(FFmpeg::avutil UNKNOWN IMPORTED)
    set_target_properties(FFmpeg::avutil PROPERTIES
      IMPORTED_LOCATION "${FFmpeg_AVUTIL_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_AVCODEC_INCLUDE_DIR}"
    )
  endif()
  if(FFmpeg_SWRESAMPLE_LIBRARY AND NOT TARGET FFmpeg::swresample)
    add_library(FFmpeg::swresample UNKNOWN IMPORTED)
    set_target_properties(FFmpeg::swresample PROPERTIES
      IMPORTED_LOCATION "${FFmpeg_SWRESAMPLE_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_AVCODEC_INCLUDE_DIR}"
    )
  endif()
endif()

mark_as_advanced(
  FFmpeg_AVCODEC_INCLUDE_DIR
  FFmpeg_AVCODEC_LIBRARY
  FFmpeg_AVFORMAT_LIBRARY
  FFmpeg_AVUTIL_LIBRARY
  FFmpeg_SWRESAMPLE_LIBRARY
)
