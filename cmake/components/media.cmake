# media.cmake -- caudio::media (reader/decoder/FFmpeg metadata).
set(CAUDIO_MEDIA_MODULE_SOURCES
  modules/caudio/media.cppm
  modules/caudio/media/reader.cppm
  modules/caudio/media/decoder.cppm
  modules/caudio/media/audio_ext.cppm
)
set(CAUDIO_MEDIA_SOURCES
  src/media/reader.cpp
  src/media/decoder.cpp
  src/media/decoders/ffmpeg.cpp
)
caudio_add_component(media SOURCES ${CAUDIO_MEDIA_SOURCES} MODULE_SOURCES ${CAUDIO_MEDIA_MODULE_SOURCES} DEPS caudio::utils caudio::types)
# FFmpeg (sole decoder backend) is used only by src/media/decoders/ffmpeg.cpp
# (via ffmpeg_impl.hpp), so link it here PRIVATE. STATIC private deps propagate
# as $<LINK_ONLY:...> to the final executables, so consumers still resolve
# libav symbols.
target_link_libraries(media PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)

if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(media EXTRA_DEPS caudio::utils_shared)
  target_link_libraries(media_shared PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
endif()
