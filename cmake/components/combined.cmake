# combined is the opt-in shared libcaudio: utils/player/db/engine/media
# linked into a single shared lib. ipc/service/client/app/config ship
# as separate *_shared targets (daemon IPC has no shared ABI); config sources
# compile into ipc statically. Built whenever CAUDIO_BUILD_SHARED is on.
#
# Links component object files directly instead of WHOLE_ARCHIVE-ing static
# archives. This avoids duplicate symbols from overlapping transitive deps
# (player->media, db->media, engine->player+media+utils, all->utils) and
# removes the need for --allow-multiple-definition.
if(CAUDIO_BUILD_SHARED)
  add_library(combined SHARED)
  add_library(caudio::combined ALIAS combined)
  set_target_properties(combined PROPERTIES OUTPUT_NAME caudio VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR} PDB_NAME caudio_)
  target_include_directories(combined PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(combined PUBLIC cxx_std_23)
  target_sources(combined PRIVATE src/_stub/shared_stub.cpp)
  # Object libraries are compiled once with PIC; linking them directly pulls
  # each object file exactly once. Vendor static libs (sqlite3, blake3) are
  # linked normally -- the linker pulls only needed objects.
  target_sources(combined PRIVATE
    $<TARGET_OBJECTS:utils_objects>
    $<TARGET_OBJECTS:config_objects>
    $<TARGET_OBJECTS:media_objects>
    $<TARGET_OBJECTS:player_objects>
    $<TARGET_OBJECTS:db_objects>
    $<TARGET_OBJECTS:engine_objects>
    $<TARGET_OBJECTS:ipc_objects>
    $<TARGET_OBJECTS:service_objects>
    $<TARGET_OBJECTS:client_objects>
    $<TARGET_OBJECTS:app_objects>
  )
  target_link_libraries(combined PRIVATE
    caudio::sqlite3
    blake3
    tag
    FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample
    Threads::Threads)
  if(UNIX AND NOT APPLE)
    target_link_libraries(combined PRIVATE rt)
  endif()
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(combined)
  endif()
endif()
