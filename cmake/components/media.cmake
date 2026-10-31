# media.cmake -- caudio::media (reader/decoder/FFmpeg metadata/scan discovery).
set(CAUDIO_MEDIA_MODULE_SOURCES
  modules/caudio/media.cppm
  modules/caudio/media/reader.cppm
  modules/caudio/media/source.cppm
  modules/caudio/media/decoder.cppm
  modules/caudio/media/audio_ext.cppm
  modules/caudio/media/scan.cppm
)
set(CAUDIO_MEDIA_SOURCES
  src/media/reader.cpp
  src/media/source.cpp
  src/media/decoder.cpp
  src/media/decoders/ffmpeg.cpp
  src/media/scan.cpp
  src/media/fingerprint.cpp
)
caudio_add_component(media SOURCES ${CAUDIO_MEDIA_SOURCES} MODULE_SOURCES ${CAUDIO_MEDIA_MODULE_SOURCES} DEPS caudio::utils caudio::types)
ca_set_module_warnings(media)
# FFmpeg (sole decoder backend) is used only by src/media/decoders/ffmpeg.cpp
# (via ffmpeg_impl.hpp), so link it here PRIVATE. STATIC private deps propagate
# as $<LINK_ONLY:...> to the final executables, so consumers still resolve
# libav symbols.
target_link_libraries(media PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
# BLAKE3 moved here with the scan discovery half (src/media/scan.cpp full-file
# hash + src/media/fingerprint.cpp sampled hash); db no longer hashes.
target_link_libraries(media PRIVATE blake3)

# Propagate FFmpeg include dirs to the object library so it can compile
# media's .cpp files against FFmpeg headers. The object library is not a
# link target, so target_link_libraries would not give it compile-time
# includes. Needed in both static and shared builds because media_objects
# compiles the same sources as the static media library.
# SYSTEM: FFmpeg headers trigger /WX warnings (C4244) under MSVC; treat as
# system includes so they don't fail warnings-as-errors builds.
get_target_property(_ff_inc FFmpeg::avcodec INTERFACE_INCLUDE_DIRECTORIES)
if(_ff_inc)
  target_include_directories(media_objects SYSTEM PRIVATE ${_ff_inc})
endif()

if(CAUDIO_BUILD_SHARED)
  caudio_add_shared_variant(media EXTRA_DEPS caudio::utils_shared)
  if(CAUDIO_BUILD_STATIC)
    target_link_libraries(media_shared PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample blake3)
  endif()
endif()
