# Install targets, exports, docs, man page, license, systemd service.
# Component targets are registered in CAUDIO_INSTALL_LIBS below.
# Components are registered via caudio_add_component / caudio_add_shared_variant;
# the install list here must stay in sync with component definitions.
set(CAUDIO_INSTALL_LIBS
  types
  utils
  config
  media
  player
  db
  engine
  ipc
  client
  service
  app
  sqlite3
  blake3
  tag
)
if(CAUDIO_BUILD_SHARED)
  list(APPEND CAUDIO_INSTALL_LIBS
    utils_shared
    media_shared
    player_shared
    db_shared
    engine_shared
    client_shared
    service_shared
    app_shared
    combined
  )
endif()
if(CAUDIO_ENABLE_MODULES)
  list(APPEND CAUDIO_INSTALL_LIBS caudio_umbrella)
endif()

if(CAUDIO_ENABLE_MODULES)
  install(
    TARGETS ${CAUDIO_INSTALL_LIBS}
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT dev
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    # Module sources live at <prefix>/modules/ (NOT under include/):
    # they are build inputs for BMI consumers, not C++ headers.
    FILE_SET CXX_MODULES DESTINATION modules COMPONENT dev
  )
else()
  install(
    TARGETS ${CAUDIO_INSTALL_LIBS}
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT dev
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()

# Version header
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version_config.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio COMPONENT dev)

# Public headers
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev FILES_MATCHING PATTERN "*.hpp")

# NOTE: vendor headers (sqlite3.h, blake3.h) are deliberately NOT installed --
# no public header references them (fwd-decls only). Only the static archives
# below ship (transitive link deps). miniaudio.h was never installed either.

install(FILES cmake/FindFFmpeg.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio COMPONENT dev)
install(EXPORT caudioTargets NAMESPACE caudio:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio COMPONENT dev)

if(CAUDIO_BUILD_CLI)
  install(TARGETS caudio_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
endif()

# Man page -- roff source tracked at docs/man/caudio.1
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/man/caudio.1")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/man/caudio.1"
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1" COMPONENT runtime)
endif()

# License at the archive top level (CPACK_RESOURCE_FILE_LICENSE is not
# bundled into plain TGZ/ZIP archives).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION . COMPONENT runtime)
endif()

# Systemd user service
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/caudio.service")
  install(FILES packaging/caudio.service DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/systemd/user COMPONENT runtime)
endif()
