# caudio project options and test registration.

option(CAUDIO_BUILD_TESTS "Build unit tests" ON)
option(CAUDIO_BUILD_SHARED "Build caudio as a shared library" ON)
option(CA_RUNTIME_BUILD_SHARED "Build ca_runtime as a shared library" ${CAUDIO_BUILD_SHARED})
option(CAUDIO_USE_SYSTEM_CSTDX "Use an installed cstdx instead of the in-tree copy" OFF)
option(CAUDIO_ENABLE_CCACHE "use ccache/sccache compiler launcher if found (opt-in)" ON)

# POSIX feature-test macros for caudio's own targets.
#
# libc consumes these macros while parsing the first system header, so they
# must be defined before any include in every translation unit. Keeping the
# values here and applying them as target_compile_definitions() is the
# single-place equivalent of repeating `#define _POSIX_C_SOURCE 200809L` at the
# top of each source file.
#
# They are applied PRIVATE (not INTERFACE): caudio's installed package must not
# impose feature-test macros on consumer code. A shared INTERFACE target cannot
# be used here because a static ca_runtime would export it as a link dependency
# that is not part of caudioTargets.
#
# On Darwin, strict _POSIX_C_SOURCE hides BSD extensions that caudio's POSIX
# layer still relies on, notably INADDR_LOOPBACK in <netinet/in.h>. Requesting
# _DARWIN_C_SOURCE restores the Darwin C library's full namespace without
# dropping the POSIX.1-2008 level.
set(CAUDIO_POSIX_DEFINITIONS
    _POSIX_C_SOURCE=200809L
    $<$<BOOL:${MINGW}>:_DEFAULT_SOURCE>
    $<$<BOOL:${APPLE}>:_DARWIN_C_SOURCE>
)

if(CAUDIO_BUILD_TESTS)
  enable_testing()
endif()

if(CAUDIO_BUILD_TESTS AND NOT DEFINED CSTDX_BUILD_TESTS)
  # cstdx defaults its tests to PROJECT_IS_TOP_LEVEL, which is false when it is
  # added as a subdirectory. Propagate the parent's choice without overriding
  # an explicit -DCSTDX_BUILD_TESTS=... on the command line.
  set(CSTDX_BUILD_TESTS ON)
endif()
