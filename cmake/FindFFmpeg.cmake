# FindFFmpeg.cmake — locates FFmpeg 8.1.2 shared build at C:/Users/Secondary/ffmpeg
# Provides imported targets FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil etc.

find_path(FFmpeg_AVCODEC_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT
  PATH_SUFFIXES include
  NO_DEFAULT_PATH
)
find_path(FFmpeg_AVCODEC_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  PATH_SUFFIXES include
)

find_library(FFmpeg_AVCODEC_LIBRARY
  NAMES avcodec libavcodec
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT
  PATH_SUFFIXES lib bin
)

find_library(FFmpeg_AVFORMAT_LIBRARY
  NAMES avformat libavformat
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT
  PATH_SUFFIXES lib bin
)

find_library(FFmpeg_AVUTIL_LIBRARY
  NAMES avutil libavutil
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT
  PATH_SUFFIXES lib bin
)

find_library(FFmpeg_SWRESAMPLE_LIBRARY
  NAMES swresample libswresample
  PATHS ${CMAKE_PREFIX_PATH} ${FFmpeg_ROOT} ENV FFmpeg_ROOT
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
