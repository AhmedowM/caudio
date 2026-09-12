add_library(combined SHARED)
set_target_properties(combined PROPERTIES OUTPUT_NAME caudio)
target_sources(combined PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(combined PUBLIC
  caudio::utils caudio::player caudio::db caudio::engine Threads::Threads)
target_link_libraries(combined PRIVATE
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::utils>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::player>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::db>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::engine>)
target_link_options(combined PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
target_link_libraries(combined PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
