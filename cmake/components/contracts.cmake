# contracts.cmake -- backend-neutral, header-only component contracts.
# Header-only (INTERFACE) in all builds: contracts ships no runtime symbols,
# only C++23 module BMIs when enabled. The .cppm sources are installed
# directly; consumers doing find_package(caudio) + import caudio.contracts
# add the install prefix/modules to their C++ module search path.
set(CAUDIO_CONTRACTS_MODULE_SOURCES
  modules/caudio/contracts.cppm
  modules/caudio/contracts/database.cppm
)
add_library(contracts INTERFACE)
add_library(caudio::contracts ALIAS contracts)
set_property(DIRECTORY APPEND PROPERTY CAUDIO_TARGETS contracts)
target_include_directories(contracts INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(contracts INTERFACE cxx_std_23)
target_link_libraries(contracts INTERFACE caudio::types caudio::utils)
if(CAUDIO_ENABLE_MODULES)
  # Install the .cppm sources so consumers can compile them into BMIs or
  # add the install/modules dir to their module search path.
  install(FILES ${CAUDIO_CONTRACTS_MODULE_SOURCES}
    DESTINATION modules/caudio/contracts COMPONENT dev)
endif()
