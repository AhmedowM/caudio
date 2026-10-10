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
#      then configures with --preset ci-trimmed. Cache hit: find_package
#      consumes the prefix directly. Cache miss: one-shot configure-time
#      source build fills it (~10 min, then cached).
#   2. Local iteration: -DCAUDIO_USE_TRIMMED_FFMPEG=ON with
#      -DCAUDIO_FFMPEG_TRIM_PREFIX=<path> (default: <build>/ffmpeg-trimmed).
# A trim outage never reds the pipeline: any build failure stops with a note
# to retry with -DCAUDIO_USE_TRIMMED_FFMPEG=OFF (default provider), and the CI
# job implements that retry.
#
# Supported builders: Linux GCC, macOS brew-LLVM, Windows MinGW (msys2).
# MSVC is unsupported (upstream FFmpeg has no native MSVC build); MSVC keeps
# the default provider.

# Pinned source matching the audited inventory. Override with
# -DCAUDIO_FFMPEG_TRIM_VERSION=x.y.z if the audit moves.
set(CAUDIO_FFMPEG_TRIM_VERSION "8.1.2" CACHE STRING "pinned FFmpeg version for trimmed builds")
set(CAUDIO_FFMPEG_TRIM_URL "https://ffmpeg.org/releases/ffmpeg-${CAUDIO_FFMPEG_TRIM_VERSION}.tar.xz"
    CACHE STRING "source tarball for trimmed FFmpeg builds")

# Where the trimmed tree installs (one-shot build target or cache restore).
set(CAUDIO_FFMPEG_TRIM_PREFIX "${CMAKE_BINARY_DIR}/ffmpeg-trimmed"
    CACHE PATH "install prefix for the trimmed FFmpeg build")

# Whitelist: --disable-all base, then re-enable exactly what the engine uses
# (decoder.cpp/resample path needs avcodec+swresample; extractMetadata/scan
# needs avformat; everything links avutil).
set(CAUDIO_FFMPEG_TRIM_FLAGS
  --enable-shared --disable-static
  --enable-pic
  --disable-all
  --disable-autodetect
  --enable-avcodec --enable-avformat --enable-avutil --enable-swresample
  --disable-avfilter --disable-avdevice --disable-swscale --disable-postproc
  --disable-programs --disable-doc --disable-debug
  --disable-network
  --disable-hwaccels
  --disable-encoders --disable-muxers --disable-filters
  --disable-indevs --disable-outdevs
  --enable-zlib
  # Decoders: pcm_* family + the ~40 named audio decoders. Native
  # implementations only (no libmp3lame/libopus/libvorbis/libgsm/libspeex/
  # libopencore_* external deps). Name fixes vs review text: mpc7/mpc8 (not
  # musepack7/8), amrnb/amrwb (not amr_nb/amr_wb), dca (not dts).
  --enable-decoder=pcm_alaw --enable-decoder=pcm_bluray --enable-decoder=pcm_dvd
  --enable-decoder=pcm_f16le --enable-decoder=pcm_f24le
  --enable-decoder=pcm_f32be --enable-decoder=pcm_f32le
  --enable-decoder=pcm_f64be --enable-decoder=pcm_f64le
  --enable-decoder=pcm_lxf --enable-decoder=pcm_mulaw
  --enable-decoder=pcm_s8 --enable-decoder=pcm_s8_planar
  --enable-decoder=pcm_s16be --enable-decoder=pcm_s16be_planar
  --enable-decoder=pcm_s16le --enable-decoder=pcm_s16le_planar
  --enable-decoder=pcm_s24be --enable-decoder=pcm_s24daud
  --enable-decoder=pcm_s24le --enable-decoder=pcm_s24le_planar
  --enable-decoder=pcm_s32be --enable-decoder=pcm_s32le --enable-decoder=pcm_s32le_planar
  --enable-decoder=pcm_s64be --enable-decoder=pcm_s64le
  --enable-decoder=pcm_sga
  --enable-decoder=pcm_u8 --enable-decoder=pcm_u16be --enable-decoder=pcm_u16le
  --enable-decoder=pcm_u24be --enable-decoder=pcm_u24le
  --enable-decoder=pcm_u32be --enable-decoder=pcm_u32le
  --enable-decoder=pcm_vidc
  --enable-decoder=adpcm_ms --enable-decoder=adpcm_ima_wav
  --enable-decoder=flac --enable-decoder=mp3float --enable-decoder=vorbis --enable-decoder=opus
  --enable-decoder=aac --enable-decoder=aac_latm --enable-decoder=alac --enable-decoder=ape
  --enable-decoder=mpc7 --enable-decoder=mpc8
  --enable-decoder=wavpack --enable-decoder=tta --enable-decoder=tak --enable-decoder=shorten
  --enable-decoder=wmalossless --enable-decoder=wmapro --enable-decoder=wmav1 --enable-decoder=wmav2
  --enable-decoder=ac3 --enable-decoder=eac3 --enable-decoder=dca
  --enable-decoder=truehd --enable-decoder=mlp
  --enable-decoder=dsd_lsbf --enable-decoder=dsd_msbf
  --enable-decoder=dsd_lsbf_planar --enable-decoder=dsd_msbf_planar
  --enable-decoder=amrnb --enable-decoder=amrwb
  --enable-decoder=speex --enable-decoder=gsm
  # Demuxers: mov covers mov/mp4/m4a, matroska covers mka/webm audio, ogg
  # covers .ogg/.opus. No standalone `opus` demuxer exists (review text fix).
  --enable-demuxer=wav --enable-demuxer=w64 --enable-demuxer=aiff --enable-demuxer=au
  --enable-demuxer=caf --enable-demuxer=flac --enable-demuxer=mp3 --enable-demuxer=ogg
  --enable-demuxer=mov --enable-demuxer=aac --enable-demuxer=asf
  --enable-demuxer=ape --enable-demuxer=mpc --enable-demuxer=mpc8 --enable-demuxer=wv
  --enable-demuxer=tta --enable-demuxer=amr --enable-demuxer=matroska
  --enable-demuxer=ac3 --enable-demuxer=eac3 --enable-demuxer=dts
  --enable-demuxer=truehd --enable-demuxer=mlp --enable-demuxer=spdif
  --enable-demuxer=voc --enable-demuxer=ircam --enable-demuxer=sox
  --enable-demuxer=gsm --enable-demuxer=sbc
  # Parsers needed by the kept demuxer/decoder pairs.
  --enable-parser=aac --enable-parser=aac_latm --enable-parser=ac3 --enable-parser=dca
  --enable-parser=flac --enable-parser=mpegaudio --enable-parser=opus
  --enable-parser=vorbis --enable-parser=tak
  # Protocols: local files only. Internet radio (http/hls + TLS) stays a
  # future re-enable, never silent.
  --enable-protocol=file --enable-protocol=pipe
  # Bitstream filters: raw-.aac reshape + core-extraction + rechunk helpers.
  --enable-bsf=aac_adtstoasc --enable-bsf=opus_metadata --enable-bsf=truehd_core
  --enable-bsf=eac3_core --enable-bsf=dca_core --enable-bsf=pcm_rechunk
  --enable-bsf=dump_extra
  CACHE STRING "configure flags for the trimmed decode-only FFmpeg build")

# Honors a prebuilt trimmed tree via FFmpeg_ROOT; otherwise performs a
# one-shot configure-time source build into CAUDIO_FFMPEG_TRIM_PREFIX (needs
# a POSIX shell + make + nasm + zlib; first build takes ~10 min, CI caches the
# prefix). System paths are never consulted in trimmed mode: without
# FFmpeg_ROOT the build would silently use a full FFmpeg and the whitelist
# would guard nothing.
function(caudio_setup_trimmed_ffmpeg)
  if(MSVC)
    message(FATAL_ERROR "CAUDIO_USE_TRIMMED_FFMPEG is unsupported with MSVC "
      "(upstream FFmpeg has no native MSVC build) -- use MinGW or the default provider")
  endif()
  if(FFmpeg_ROOT OR DEFINED ENV{FFmpeg_ROOT})
    find_package(FFmpeg QUIET)
    if(FFmpeg_FOUND)
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
  if(WIN32)
    find_program(_trim_bash bash REQUIRED)
    find_program(_trim_make make REQUIRED)
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
  separate_arguments(_trim_flags UNIX_COMMAND "${CAUDIO_FFMPEG_TRIM_FLAGS}")
  if(WIN32)
    set(_trim_configure ${_trim_sh} "${_trim_src}/configure")
  else()
    set(_trim_configure "${_trim_src}/configure")
  endif()
  message(STATUS "Building trimmed FFmpeg ${CAUDIO_FFMPEG_TRIM_VERSION} into ${_trim_prefix} "
    "(decode-only whitelist; one-shot configure-time build)")
  execute_process(
    COMMAND ${_trim_configure} --prefix=${_trim_prefix} ${_trim_flags}
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
