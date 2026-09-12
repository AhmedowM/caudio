add_library(caudio_all SHARED)
target_sources(caudio_all PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_all PUBLIC
  caudio::utils caudio::player caudio::db caudio::engine Threads::Threads)
target_link_libraries(caudio_all PRIVATE
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::utils>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::player>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::db>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::engine>)
target_link_options(caudio_all PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
target_link_libraries(caudio_all PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
