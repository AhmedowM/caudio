# combined is the opt-in shared libcaudio: utils/player/db/engine/media
# linked into a single shared lib. ipc/service/client/app/config ship
# as separate *_shared targets (daemon IPC has no shared ABI); config sources
# compile into ipc statically. Gated on CAUDIO_BUILD_SHARED (OFF by default).
#
# Links component object files directly instead of WHOLE_ARCHIVE-ing static
# archives. This avoids duplicate symbols from overlapping transitive deps
# (player->media, db->media, engine->player+media+utils, all->utils) and
# removes the need for --allow-multiple-definition.
if(CAUDIO_BUILD_SHARED)
  add_library(combined SHARED)
  add_library(caudio::combined ALIAS combined)
  set_target_properties(combined PROPERTIES OUTPUT_NAME caudio VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR} PDB_NAME caudio_shared)
  target_sources(combined PRIVATE src/_stub/shared_stub.cpp)
  # Object libraries are compiled once with PIC; linking them directly pulls
  # each object file exactly once. Vendor static libs (sqlite3, blake3) are
  # linked normally -- the linker pulls only needed objects.
  target_link_libraries(combined PRIVATE
    caudio::utils_objects
    caudio::player_objects
    caudio::db_objects
    caudio::engine_objects
    caudio::media_objects
    caudio::sqlite3
    blake3
    FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample
    Threads::Threads)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(combined)
  endif()
endif()
