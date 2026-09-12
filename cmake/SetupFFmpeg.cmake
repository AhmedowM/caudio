# SetupFFmpeg.cmake — cascading FFmpeg provider
# Priority:
# 1. System (find_package with hints from FindFFmpeg.cmake)
# 2. vcpkg / Conan (if available)
# 3. Prebuilt download (FetchContent, only needed libs)
# 4. Build from source (ExternalProject)
# 5. Fail

include(FindPackageHandleStandardArgs)

function(caudio_setup_ffmpeg)
  # 1. System search via FindFFmpeg.cmake (already handles FFmpeg_ROOT, PATH, USERPROFILE/ffmpeg)
  find_package(FFmpeg QUIET)
  if(FFmpeg_FOUND)
    message(STATUS "FFmpeg found (system): ${FFmpeg_AVCODEC_LIBRARY}")
    set(FFmpeg_FOUND TRUE PARENT_SCOPE)
    return()
  endif()
  message(STATUS "FFmpeg not found on system, trying vcpkg/Conan...")

  # 2. vcpkg
  if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(STATUS "Trying vcpkg at $ENV{VCPKG_ROOT}...")
    find_package(FFmpeg QUIET CONFIG)
    if(FFmpeg_FOUND)
      message(STATUS "FFmpeg found via vcpkg")
      set(FFmpeg_FOUND TRUE PARENT_SCOPE)
      return()
    endif()
  endif()
  # Also try vcpkg toolchain's find
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG_PKG libavcodec libavformat libavutil libswresample QUIET)
    if(FFMPEG_PKG_FOUND)
      message(STATUS "FFmpeg found via pkg-config (vcpkg/conan)")
      # Create imported targets from pkg-config
      foreach(_lib IN LISTS FFMPEG_PKG_LIBRARIES)
        if(NOT TARGET FFmpeg::${_lib})
          add_library(FFmpeg::${_lib} UNKNOWN IMPORTED)
        endif()
      endforeach()
      set(FFmpeg_FOUND TRUE PARENT_SCOPE)
      return()
    endif()
  endif()
  # Conan
  find_program(CONAN_CMD conan)
  if(CONAN_CMD)
    message(STATUS "Conan found at ${CONAN_CMD}, try 'conan install .' if needed")
  endif()
  message(STATUS "vcpkg/Conan not found, trying prebuilt download...")

  # 3. Prebuilt download (FetchContent) — Windows only for now, Linux/macOS use system
  if(WIN32)
    include(FetchContent)
    # Gyan FFmpeg 9.0.1 shared full — ~500MB, contains bin/*.dll + lib/*.lib + include
    # Latest Windows build from gyan.dev
    set(_ffmpeg_url "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full-shared.7z")
    message(STATUS "Fetching FFmpeg prebuilt from ${_ffmpeg_url}...")
    FetchContent_Declare(
      ffmpeg_prebuilt
      URL ${_ffmpeg_url}
    )
    # Try to fetch, but don't fail hard if network unavailable
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_MakeAvailable(ffmpeg_prebuilt)
    if(EXISTS "${ffmpeg_prebuilt_SOURCE_DIR}/bin/ffmpeg.exe")
      set(_prebuilt_root "${ffmpeg_prebuilt_SOURCE_DIR}")
      message(STATUS "FFmpeg prebuilt extracted to ${_prebuilt_root}")
      # Hint FindFFmpeg to this dir
      set(FFmpeg_ROOT "${_prebuilt_root}" CACHE PATH "FFmpeg root from prebuilt" FORCE)
      find_package(FFmpeg QUIET)
      if(FFmpeg_FOUND)
        message(STATUS "FFmpeg found via prebuilt: ${FFmpeg_AVCODEC_LIBRARY}")
        set(FFmpeg_FOUND TRUE PARENT_SCOPE)
        return()
      endif()
    endif()
    message(STATUS "Prebuilt fetch failed, trying source build...")
  else()
    message(STATUS "Prebuilt download skipped on non-Windows, trying source build...")
  endif()

  # 4. Build from source (ExternalProject) — last resort, slow
  include(ExternalProject)
  message(STATUS "Fetching FFmpeg source to build (this will take 10+ minutes, requires yasm/nasm)...")
  ExternalProject_Add(ffmpeg_external
    URL https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --enable-shared --disable-static --enable-gpl --prefix=<INSTALL_DIR> --disable-doc --disable-ffplay --disable-ffprobe
    BUILD_COMMAND make -j4
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE FALSE
  )
  # After build, set FFmpeg_ROOT to install dir and retry
  ExternalProject_Get_Property(ffmpeg_external install_dir)
  set(FFmpeg_ROOT "${install_dir}" CACHE PATH "FFmpeg root from source build" FORCE)
  # Note: need to re-run find_package after build completes (user must re-configure)
  message(WARNING "FFmpeg source build configured. Re-run cmake after it finishes: cmake --build build --target ffmpeg_external")

  # 5. Fail
  message(FATAL_ERROR "FFmpeg not found and all fallback providers failed. Install FFmpeg via: winget install ffmpeg / brew install ffmpeg / apt install libavcodec-dev, or set -DFFmpeg_ROOT=/path/to/ffmpeg")
endfunction()
