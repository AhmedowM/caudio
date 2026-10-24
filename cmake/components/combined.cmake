# combined is the opt-in shared libcaudio: utils/player/db/engine/media
# whole-archived into a single shared lib. ipc/service/client/app/config ship
# as separate *_shared targets (daemon IPC has no shared ABI); config sources
# compile into ipc statically. Gated on CAUDIO_BUILD_SHARED (OFF by default).
if(CAUDIO_BUILD_SHARED)
  add_library(combined SHARED)
  add_library(caudio::combined ALIAS combined)
  set_target_properties(combined PROPERTIES OUTPUT_NAME caudio VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
  target_sources(combined PRIVATE src/_stub/shared_stub.cpp)
  target_link_libraries(combined PRIVATE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::utils>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::player>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::db>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::engine>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::media>
    FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample
    Threads::Threads)
  target_link_options(combined PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wl,--allow-multiple-definition>)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(combined)
  endif()
endif()
