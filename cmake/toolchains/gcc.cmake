# Canonical toolchain: GCC is the only CI/Release compiler.
# Pins the compiler explicitly so builds don't depend on PATH order.
if (NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER gcc)
endif()
if (NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER g++)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/detail/strip.cmake")
