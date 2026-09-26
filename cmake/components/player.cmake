set(CAUDIO_PLAYER_MODULE_SOURCES
  modules/player/player.cppm
  modules/player/player_core.cppm
  modules/player/reader.cppm
  modules/player/output.cppm
  modules/player/decoder.cppm
  modules/player/decoders/decoder_interface.cppm
  modules/player/decoders/ffmpeg.cppm
)
set(CAUDIO_PLAYER_SOURCES
  src/player/miniaudio_impl.cpp
  src/player/reader.cpp
  src/player/output.cpp
  src/player/decoder.cpp
  src/player/player_core.cpp
  src/player/decoders/ffmpeg.cpp
)
caudio_add_component(player SOURCES ${CAUDIO_PLAYER_SOURCES} MODULE_SOURCES ${CAUDIO_PLAYER_MODULE_SOURCES} DEPS caudio::utils INCLUDES vendor)
target_compile_options(player PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)

caudio_add_shared_variant(player EXTRA_DEPS caudio::utils_shared)
target_link_libraries(player_shared PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
