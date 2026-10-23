# FFmpegTrimmed.cmake -- decode-only FFmpeg whitelist (post-1.0 packaging track)
#
# Builds a trimmed shared FFmpeg from a pinned source tarball with an explicit
# --enable-* whitelist. Component names below were verified against the
# audited inventory (Gyan 8.1.2 full) via `ffmpeg -decoders/-demuxers/-bsfs`
# plus ffprobe format probes of tests/fixtures/*; corrections vs the review
# text are marked. The explicit whitelist fails configure loudly on missing
# deps -- never silently drops a format.
#
# Product premise: decode-only. Tag edits write SQLite rows (file sync, when
# it lands, uses a dedicated tag library -- TagLib/lofty -- never FFmpeg
# muxers), so no muxers/encoders are kept. Supported formats = whatever
# demuxers stay enabled. Shared libs throughout (no new LGPL burden);
# full-static stays rejected.
#
# Consumption (Windows first):
#   1. CI restores the cached install prefix into CAUDIO_FFMPEG_TRIM_PREFIX
#      (key: hash of this file, which embeds CAUDIO_FFMPEG_TRIM_VERSION),
#      then configures the ci intent with --toolchain gcc --trimmed-ffmpeg.
#      Cache hit: find_package
#      consumes the prefix directly. Cache miss: one-shot configure-time
#      source build fills it (~10 min, then cached).
#   2. Local iteration: -DCAUDIO_USE_TRIMMED_FFMPEG=ON with
#      -DCAUDIO_FFMPEG_TRIM_PREFIX=<path> (default: <build>/ffmpeg-trimmed).
# A trim outage never reds the pipeline: any build failure stops with a note
# to retry with -DCAUDIO_USE_TRIMMED_FFMPEG=OFF (default provider), and the CI
# job implements that retry.
#
# Supported builders: Linux GCC, macOS brew-LLVM, Windows MinGW (msys2).
# MSVC cannot compile FFmpeg (no native upstream MSVC build) but links a
# prebuilt trimmed tree via FFmpeg_ROOT (COFF import libs in bin/).

# Pinned source matching the audited inventory. Override with
# -DCAUDIO_FFMPEG_TRIM_VERSION=x.y.z if the audit moves.
set(CAUDIO_FFMPEG_TRIM_VERSION "8.1.2" CACHE STRING "pinned FFmpeg version for trimmed builds")
set(CAUDIO_FFMPEG_TRIM_URL "https://ffmpeg.org/releases/ffmpeg-${CAUDIO_FFMPEG_TRIM_VERSION}.tar.xz"
    CACHE STRING "source tarball for trimmed FFmpeg builds")

set(CAUDIO_FFMPEG_TRIM_PREFIX "${CMAKE_BINARY_DIR}/ffmpeg"
    CACHE PATH "install prefix for the trimmed FFmpeg build")

# Decode-only whitelist: --disable-all base, then re-enable exactly what the
# engine uses (decoder.cpp/resample needs avcodec+swresample; extractMetadata/
# scan needs avformat; everything links avutil). One source of truth used by
# the cmake module AND the producer workflow. Flags live in
# cmake/ffmpeg_trimmed_flags.txt (one per line) to avoid regex parsing.
file(READ "${CMAKE_CURRENT_LIST_DIR}/ffmpeg_trimmed_flags.txt" _trim_flags_raw)
separate_arguments(FFMPEG_TRIM_FLAGS NATIVE_COMMAND "${_trim_flags_raw}")
unset(_trim_flags_raw)
set(CAUDIO_FFMPEG_TRIM_FLAGS "${FFMPEG_TRIM_FLAGS}")

# Honors a prebuilt trimmed tree via FFmpeg_ROOT; otherwise performs a
# one-shot configure-time source build into CAUDIO_FFMPEG_TRIM_PREFIX (needs
# a POSIX shell + make + nasm + zlib; first build takes ~10 min, CI caches the
# prefix). System paths are never consulted in trimmed mode: without
# FFmpeg_ROOT the build would silently use a full FFmpeg and the whitelist
# would guard nothing.
function(caudio_setup_trimmed_ffmpeg)
  # Drop stale hits: find_library/include results persist in cache across
  # reconfigures, so a previous system/Gyan hit would shadow the trimmed
  # tree even after FFmpeg_ROOT moves.
  foreach(_ff_var IN ITEMS FFmpeg_AVCODEC_INCLUDE_DIR FFmpeg_AVCODEC_LIBRARY
      FFmpeg_AVFORMAT_LIBRARY FFmpeg_AVUTIL_LIBRARY FFmpeg_SWRESAMPLE_LIBRARY)
    unset(${_ff_var} CACHE)
  endforeach()
  unset(FFmpeg_FOUND)
  unset(FFmpeg_FOUND CACHE)
  if(FFmpeg_ROOT OR DEFINED ENV{FFmpeg_ROOT})
    find_package(FFmpeg QUIET)
    if(FFmpeg_FOUND)
      # MSVC links a prebuilt trimmed tree fine (COFF import libs in bin/);
      # it just cannot compile one (no native upstream MSVC build).
      message(STATUS "Trimmed FFmpeg found: ${FFmpeg_AVCODEC_LIBRARY}")
      set(FFmpeg_FOUND TRUE PARENT_SCOPE)
      return()
    endif()
    message(STATUS "FFmpeg_ROOT holds no complete trimmed tree -- building into it")
    if(FFmpeg_ROOT)
      set(_trim_prefix "${FFmpeg_ROOT}")
    else()
      set(_trim_prefix "$ENV{FFmpeg_ROOT}")
    endif()
  else()
    set(_trim_prefix "${CAUDIO_FFMPEG_TRIM_PREFIX}")
  endif()
  if(MSVC)
    message(FATAL_ERROR "CAUDIO_USE_TRIMMED_FFMPEG needs a prebuilt trimmed tree "
      "under FFmpeg_ROOT with MSVC (upstream FFmpeg has no native MSVC build) -- "
      "use MinGW or the default provider")
  endif()
  if(WIN32)
    # No prebuilt tree: source build needs a POSIX env. Check explicitly so
    # the failure names the fix (a bare find_program REQUIRED error does not).
    find_program(_trim_bash bash)
    find_program(_trim_make make)
    if(NOT _trim_bash OR NOT _trim_make)
      message(FATAL_ERROR "CAUDIO_USE_TRIMMED_FFMPEG without a prebuilt trimmed tree "
        "must compile FFmpeg from source, which needs bash+make on PATH (MSYS2) plus "
        "nasm/zlib. Either set FFmpeg_ROOT (or CAUDIO_TRIMMED_ROOT) to a complete "
        "trimmed prefix -- e.g. the deps-private asset -- or install the tools. "
        "To fall back to the default provider: -DCAUDIO_USE_TRIMMED_FFMPEG=OFF")
    endif()
    set(_trim_sh ${_trim_bash})
    set(_trim_make ${_trim_make})
  else()
    find_program(_trim_make make REQUIRED)
    set(_trim_make ${_trim_make})
  endif()
  include(FetchContent)
  FetchContent_Declare(ffmpeg_trimmed_src URL ${CAUDIO_FFMPEG_TRIM_URL})
  FetchContent_MakeAvailable(ffmpeg_trimmed_src)
  set(_trim_src "${ffmpeg_trimmed_src_SOURCE_DIR}")
  set(_trim_build "${CMAKE_BINARY_DIR}/_ffmpeg_trimmed_build")
  file(MAKE_DIRECTORY "${_trim_build}" "${_trim_prefix}")
  # NOTE: CAUDIO_FFMPEG_TRIM_FLAGS is already a CMake list (one flag per
  # item). Do NOT run it through separate_arguments: the items contain no
  # spaces, so re-parsing only risks rejoining them into one shell word.
  if(WIN32)
    set(_trim_configure ${_trim_sh} "${_trim_src}/configure")
  else()
    set(_trim_configure "${_trim_src}/configure")
  endif()
  message(STATUS "Building trimmed FFmpeg ${CAUDIO_FFMPEG_TRIM_VERSION} into ${_trim_prefix} "
    "(decode-only whitelist; one-shot configure-time build)")
  execute_process(
    COMMAND ${_trim_configure} --prefix=${_trim_prefix} ${CAUDIO_FFMPEG_TRIM_FLAGS}
    WORKING_DIRECTORY "${_trim_build}"
    RESULT_VARIABLE _trim_rc
    OUTPUT_VARIABLE _trim_out
    ERROR_VARIABLE _trim_out
  )
  if(_trim_rc)
    string(SUBSTRING "${_trim_out}" 0 3000 _trim_tail)
    message(FATAL_ERROR "Trimmed FFmpeg configure failed:\n${_trim_tail}\n"
      "To fall back to the default provider, re-configure with -DCAUDIO_USE_TRIMMED_FFMPEG=OFF")
  endif()
  execute_process(
    COMMAND ${_trim_make} -j4
    WORKING_DIRECTORY "${_trim_build}"
    RESULT_VARIABLE _trim_rc
  )
  if(_trim_rc)
    message(FATAL_ERROR "Trimmed FFmpeg build failed. "
      "To fall back to the default provider, re-configure with -DCAUDIO_USE_TRIMMED_FFMPEG=OFF")
  endif()
  execute_process(
    COMMAND ${_trim_make} install
    WORKING_DIRECTORY "${_trim_build}"
    RESULT_VARIABLE _trim_rc
  )
  if(_trim_rc)
    message(FATAL_ERROR "Trimmed FFmpeg install failed. "
      "To fall back to the default provider, re-configure with -DCAUDIO_USE_TRIMMED_FFMPEG=OFF")
  endif()
  set(FFmpeg_ROOT "${_trim_prefix}" CACHE PATH "FFmpeg root from trimmed build" FORCE)
  find_package(FFmpeg QUIET)
  if(NOT FFmpeg_FOUND)
    message(FATAL_ERROR "Trimmed FFmpeg built but not found under ${_trim_prefix}. "
      "To fall back to the default provider, re-configure with -DCAUDIO_USE_TRIMMED_FFMPEG=OFF")
  endif()
  message(STATUS "Trimmed FFmpeg built: ${FFmpeg_AVCODEC_LIBRARY}")
  set(FFmpeg_FOUND TRUE PARENT_SCOPE)
endfunction()
