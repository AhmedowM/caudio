# MSVC toolchain (local dev only; CI/Release is GCC).
# Prefer the Visual Studio generator presets (dev-msvc, ...) -- they locate
# the toolchain themselves. Ninja + MSVC additionally needs vcvars in the
# calling shell, checked below with a readable error.
set(CMAKE_C_COMPILER cl)
set(CMAKE_CXX_COMPILER cl)

if(CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{VCINSTALLDIR})
  message(FATAL_ERROR
    "MSVC + Ninja requires the VS environment in this shell.\n"
    "Run vcvars64.bat from your Visual Studio installation\n"
    "(<VSInstallDir>\\VC\\Auxiliary\\Build\\vcvars64.bat), then re-configure\n"
    "-- or use a Visual Studio generator preset instead.")
endif()
