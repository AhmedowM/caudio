/**
 * @file src/caudio/umbrella.c
 * @brief caudio umbrella shared-library entry point.
 *
 * The caudio DLL is an umbrella that re-exports cstdx and ca_runtime. This
 * translation unit exists so the shared library has at least one object file.
 * Component implementations live in their own DLLs (ca_runtime.dll, …) and
 * are linked into caudio at build time; consumers link only against
 * caudio::caudio.
 */

#include <caudio.h>
