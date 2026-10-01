# Canonical toolchain: GCC is the only CI/Release compiler.
# Pins the compiler explicitly so builds don't depend on PATH order.
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
