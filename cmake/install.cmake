# caudio install and package export.

include(CMakePackageConfigHelpers)

install(TARGETS caudio ca_runtime
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(FILES "${PROJECT_BINARY_DIR}/include/caudio/version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio
)

install(EXPORT caudioTargets
    FILE caudioTargets.cmake
    NAMESPACE caudio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio
)

configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/caudioConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/caudioConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio
)

# 0.x releases are not ABI-stable across minor versions, so consumers must
# request the same minor version until 1.0.
if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(_caudio_package_compat SameMinorVersion)
else()
  set(_caudio_package_compat SameMajorVersion)
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/caudioConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ${_caudio_package_compat}
)
unset(_caudio_package_compat)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/caudioConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/caudioConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio
)
