# Install targets, exports, docs, man page, license, systemd service.
# Component targets are registered in the CAUDIO_TARGETS directory property
# by caudio_add_component / caudio_add_shared_variant (cmake/CaudioHelpers.cmake).
# Vendor libs (sqlite3, blake3, tag) are appended manually.
get_directory_property(CAUDIO_INSTALL_LIBS DIRECTORY ${CMAKE_SOURCE_DIR} CAUDIO_TARGETS)
if(NOT CAUDIO_INSTALL_LIBS)
  set(CAUDIO_INSTALL_LIBS "")
endif()

# Split targets: shared DLLs go to runtime (alongside caudio.exe),
# static libs and import libs go to dev.
set(_caudio_static "")
set(_caudio_shared "")
set(_caudio_objects "")
if(CAUDIO_BUILD_STATIC)
  foreach(_tgt IN LISTS CAUDIO_INSTALL_LIBS)
    get_target_property(_type ${_tgt} TYPE)
    if(_type STREQUAL "OBJECT_LIBRARY")
      list(APPEND _caudio_objects ${_tgt})
    elseif(_type STREQUAL "SHARED_LIBRARY")
      list(APPEND _caudio_shared ${_tgt})
    else()
      list(APPEND _caudio_static ${_tgt})
    endif()
  endforeach()
  if(TARGET combined)
    list(APPEND _caudio_shared combined)
  endif()
else()
  foreach(_tgt IN LISTS CAUDIO_INSTALL_LIBS)
    get_target_property(_type ${_tgt} TYPE)
    if(_type STREQUAL "SHARED_LIBRARY")
      list(APPEND _caudio_shared ${_tgt})
    elseif(_type STREQUAL "INTERFACE_LIBRARY")
      list(APPEND _caudio_static ${_tgt})
    endif()
  endforeach()
endif()
set(CAUDIO_INSTALL_LIBS ${_caudio_static})
unset(_caudio_static)
unset(_caudio_objects)

# Vendor libs are needed as installable dependencies only for static builds.
if(CAUDIO_BUILD_STATIC)
  list(APPEND CAUDIO_INSTALL_LIBS sqlite3 blake3 tag)
endif()

# Install static libs + import libs as dev.
install(
  TARGETS ${CAUDIO_INSTALL_LIBS}
  EXPORT caudioTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT dev
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install shared DLLs as runtime (beside caudio.exe).
if(_caudio_shared)
  install(
    TARGETS ${_caudio_shared}
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev
  )
endif()

# Modules-only umbrella: install its BMI files separately.
if(CAUDIO_ENABLE_MODULES)
  install(
    TARGETS caudio_umbrella
    EXPORT caudioTargets
    FILE_SET CXX_MODULES DESTINATION modules COMPONENT dev
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
