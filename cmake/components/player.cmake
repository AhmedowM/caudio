set(CAUDIO_PLAYER_SOURCES
  src/player/player.cppm
  src/player/player_core.cppm
  src/player/reader.cppm
  src/player/output.cppm
  src/player/decoder.cppm
  src/player/decoders/decoder_common.cppm
  src/player/decoders/decoder_interface.cppm
  src/player/decoders/ffmpeg.cppm
)
caudio_add_component(player SOURCES ${CAUDIO_PLAYER_SOURCES} DEPS caudio::utils INCLUDES vendor WITH_FFMPEG)
target_sources(caudio_player PRIVATE src/player/miniaudio_impl.cpp)
target_compile_options(caudio_player PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)

caudio_add_shared_variant(player EXTRA_DEPS caudio::utils_shared)
target_link_libraries(caudio_player_shared PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
