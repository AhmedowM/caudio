# cstdx install and package export.
#
# Consumers use `find_package(cstdx CONFIG REQUIRED)` and link
# `cstdx::cstdx`. The package is relocatable: the config file is generated
# from `cmake/cstdxConfig.cmake.in` and only refers to the installed tree.

include(CMakePackageConfigHelpers)

install(TARGETS cstdx
    EXPORT cstdxTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/cstdx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# version.h is generated into the build tree.
install(FILES "${PROJECT_BINARY_DIR}/include/cstdx/version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cstdx
)

install(EXPORT cstdxTargets
    FILE cstdxTargets.cmake
    NAMESPACE cstdx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cstdx
)

configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/cstdxConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cstdxConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cstdx
)

# 0.x releases are not ABI-stable across minor versions, so consumers must
# request the same minor version until 1.0.
if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(_cstdx_package_compat SameMinorVersion)
else()
  set(_cstdx_package_compat SameMajorVersion)
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cstdxConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ${_cstdx_package_compat}
)
unset(_cstdx_package_compat)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cstdxConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cstdxConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cstdx
)
