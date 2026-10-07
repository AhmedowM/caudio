# player.cmake -- caudio::player (FFmpeg decoding + miniaudio output).
set(CAUDIO_PLAYER_MODULE_SOURCES
  modules/player.cppm
  modules/player/core.cppm
  modules/player/reader.cppm
  modules/player/output.cppm
  modules/player/decoder.cppm
)
set(CAUDIO_PLAYER_SOURCES
  src/player/miniaudio_impl.cpp
  src/player/reader.cpp
  src/player/output.cpp
  src/player/decoder.cpp
  src/player/core.cpp
  src/player/decoders/ffmpeg.cpp
)
caudio_add_component(player SOURCES ${CAUDIO_PLAYER_SOURCES} MODULE_SOURCES ${CAUDIO_PLAYER_MODULE_SOURCES} DEPS caudio::utils INCLUDES vendor)
# GCC cannot parse Apple's ObjC-blocks system headers pulled in by miniaudio's
# CoreAudio backend; Apple Clang-only. Local mac devs (Xcode Clang) keep full
# audio; GNU-on-macOS CI builds fall back to the other backends (NOAUDIO anyway).
target_compile_definitions(player PRIVATE $<$<AND:$<PLATFORM_ID:Darwin>,$<CXX_COMPILER_ID:GNU>>:MA_NO_COREAUDIO>)
if(CAUDIO_ENABLE_MODULES)
  target_compile_options(player PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
endif()

if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(player EXTRA_DEPS caudio::utils_shared)
  target_link_libraries(player_shared PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
endif()
