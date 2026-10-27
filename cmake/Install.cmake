# Install targets, exports, docs, man page, license, systemd service.
# Component targets are registered in the CAUDIO_TARGETS directory property
# by caudio_add_component / caudio_add_shared_variant (cmake/CaudioHelpers.cmake).
# Vendor libs (sqlite3, blake3, tag) are appended manually.
get_directory_property(CAUDIO_INSTALL_LIBS DIRECTORY ${CMAKE_SOURCE_DIR} CAUDIO_TARGETS)
if(NOT CAUDIO_INSTALL_LIBS)
  set(CAUDIO_INSTALL_LIBS "")
endif()

# Split into static, shared, and object libraries. Object libraries are build
# intermediates only -- never installed. Static libs ship for static linking.
# Shared libs ship for dynamic linking.
set(_caudio_static "")
set(_caudio_shared "")
set(_caudio_objects "")
foreach(_tgt IN LISTS CAUDIO_INSTALL_LIBS)
  if(_tgt MATCHES "_objects$")
    list(APPEND _caudio_objects ${_tgt})
  elseif(_tgt MATCHES "_shared$")
    list(APPEND _caudio_shared ${_tgt})
  else()
    list(APPEND _caudio_static ${_tgt})
  endif()
endforeach()
set(CAUDIO_INSTALL_LIBS ${_caudio_static})
unset(_caudio_static)
unset(_caudio_objects)

# Vendor libs are not registered via caudio_add_component -- add them manually.
list(APPEND CAUDIO_INSTALL_LIBS sqlite3 blake3 tag)

# Shared variants install alongside static libs.
list(APPEND CAUDIO_INSTALL_LIBS ${_caudio_shared})
unset(_caudio_shared)

# combined is the shared libcaudio umbrella (not registered via caudio_add_*).
if(CAUDIO_BUILD_SHARED)
  list(APPEND CAUDIO_INSTALL_LIBS combined)
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
