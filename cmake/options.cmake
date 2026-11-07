# caudio project options and test registration.

option(CAUDIO_BUILD_TESTS "Build unit tests" ON)
option(CAUDIO_BUILD_SHARED "Build caudio as a shared library" ON)
option(CA_RUNTIME_BUILD_SHARED "Build ca_runtime as a shared library" ${CAUDIO_BUILD_SHARED})
option(CAUDIO_USE_SYSTEM_CSTDX "Use an installed cstdx instead of the in-tree copy" OFF)
option(CAUDIO_ENABLE_CCACHE "use ccache/sccache compiler launcher if found (opt-in)" ON)

if(CAUDIO_BUILD_TESTS)
  enable_testing()
endif()

if(CAUDIO_BUILD_TESTS AND NOT DEFINED CSTDX_BUILD_TESTS)
  # cstdx defaults its tests to PROJECT_IS_TOP_LEVEL, which is false when it is
  # added as a subdirectory. Propagate the parent's choice without overriding
  # an explicit -DCSTDX_BUILD_TESTS=... on the command line.
  set(CSTDX_BUILD_TESTS ON)
endif()
