# player.cmake -- caudio::player (FFmpeg decoding + miniaudio output).
set(CAUDIO_PLAYER_MODULE_SOURCES
  modules/caudio/player.cppm
  modules/caudio/player/core.cppm
  modules/caudio/player/reader.cppm
  modules/caudio/player/output.cppm
  modules/caudio/player/decoder.cppm
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
# Trimmed miniaudio (CODEBASE_REVIEW.md §3 leftovers): device playback +
# enumeration only. No decoding/encoding (FFmpeg owns that), no generation,
# resource manager, node graph, engine, or built-in wav/flac/mp3 decoders.
# miniaudio.h is included only by src/player/miniaudio_impl.cpp +
# src/player/output.cpp, so PRIVATE covers every consumer.
target_compile_definitions(player PRIVATE
  MA_NO_DECODING=1
  MA_NO_ENCODING=1
  MA_NO_GENERATION=1
  MA_NO_RESOURCE_MANAGER=1
  MA_NO_NODE_GRAPH=1
  MA_NO_ENGINE=1
  MA_NO_WAV=1
  MA_NO_FLAC=1
  MA_NO_MP3=1
)
if(CAUDIO_ENABLE_MODULES)
  target_compile_options(player PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
endif()

if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(player EXTRA_DEPS caudio::utils_shared)
  target_link_libraries(player_shared PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
endif()
