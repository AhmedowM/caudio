# MSVC toolchain (explicit --toolchain msvc; CI uses it).
# Ninja + MSVC needs vcvars: scripts/configure.py discovers and runs it (or
# accepts --vcvars). Raw cmake invocations are checked below with a readable
# error. --generator vs remains the escape hatch when no vcvars is available.
set(CMAKE_C_COMPILER cl)
set(CMAKE_CXX_COMPILER cl)

if(CMAKE_GENERATOR MATCHES "Ninja")
  # vcvars64.bat exports both variables; either one means the environment is
  # ready for cl. Visual Studio generators locate the toolchain themselves and
  # do not need this guard.
  if("$ENV{VCINSTALLDIR}" STREQUAL "" AND "$ENV{VCToolsInstallDir}" STREQUAL "")
    message(FATAL_ERROR
      "MSVC + Ninja requires the VS environment in this shell.\n"
      "Run vcvars64.bat from your Visual Studio installation\n"
      "(<VSInstallDir>\\VC\\Auxiliary\\Build\\vcvars64.bat), then re-configure\n"
      "-- or use scripts/configure.py --toolchain msvc (it runs vcvars),\n"
      "or --generator vs.")
  endif()
endif()
