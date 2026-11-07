# cstdx

`cstdx` is a small, generic C23 runtime used by caudio. It provides status
codes, checked arithmetic, allocators, arenas/pools, strings, bytes, a
type-erased vector, hashing, stable sorting, and base64.

It is a standalone CMake project. It must never include a caudio header and it
does not link C++.

## Requirements

- **Language:** C23 (`-std=c23`, no GNU extensions required).
- **Compilers:** GCC 14+, Clang 19+, AppleClang 16+, Android NDK r27+.
- **Windows:** MinGW-w64 GCC or Clang using the UCRT. MSVC `cl.exe` is not
  supported.
- **CMake:** 3.28+.

C23 features are used directly: `nullptr`, `constexpr`, `auto`,
`static_assert`, `{}`, unnamed parameters, `[[maybe_unused]]`,
`<stdckdint.h>`, and `<stdbit.h>` where the toolchain provides it.

## Build, test, install

The repository ships CMake presets for the common configurations:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --install build/dev --prefix /some/prefix
```

Available presets: `dev`, `release`, `release-dbg-info`, `minsize`,
`release-lto`, `minsize-lto`, plus the sanitizer presets `asan`, `ubsan`,
`asan-ubsan`, `tsan`, and `msan`. Each preset has matching build and test
presets.

Without presets:

```sh
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
cmake --install build/dev --prefix /some/prefix
```

Consume the installed package with:

```cmake
find_package(cstdx CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE cstdx::cstdx)
```

Inside the caudio monorepo, `add_subdirectory(libs/cstdx)` is the default;
`CAUDIO_USE_SYSTEM_CSTDX=ON` switches to `find_package`.

### Options

| Option | Default | Meaning |
|---|---|---|
| `CSTDX_BUILD_TESTS` | on when top-level | Build the C unit tests |
| `CSTDX_BUILD_CPP_TESTS` | off | Build GoogleTest + RapidCheck differential tests (fetches dependencies) |
| `CSTDX_BUILD_FUZZ` | off | Build libFuzzer targets (Clang only) |
| `CSTDX_BUILD_SHARED` | on | Build a shared library instead of a static one |
| `CSTDX_BUILD_BENCHMARKS` | off | Build benchmarks (not implemented yet) |
| `CSTDX_SANITIZERS` | empty | Comma-separated sanitizers: `address`, `undefined`, `thread`, `memory`, `leak` (`asan`/`ubsan`/`tsan`/`msan` aliases accepted) |
| `CSTDX_PROJECT_VERSION` | empty | Explicit version override (`X.Y.Z[-suffix]`) |
| `CSTDX_REQUIRE_GIT_VERSION` | off | Fail configure when no `vX.Y.Z` git tag is available |

`CSTDX_ENABLE_SANITIZERS=ON` is a deprecated alias for
`CSTDX_SANITIZERS=address,undefined`. When cstdx is built as part of the caudio
monorepo, use `-DCAUDIO_SANITIZERS=...` instead so caudio and cstdx are
instrumented consistently.

### Versioning

The version is resolved, in order, from `-DCSTDX_PROJECT_VERSION=X.Y.Z[-suffix]`,
the nearest `vX.Y.Z` git tag, `libs/cstdx/VERSION`, then a `0.1.0` fallback.
The generated `<cstdx/version.h>` is installed with the library and exposes:

```c
CSTDX_VERSION        /* "0.1.0", including any pre-release suffix */
CSTDX_VERSION_MAJOR
CSTDX_VERSION_MINOR
CSTDX_VERSION_PATCH
CSTDX_GIT_HASH       /* short hash, or "unknown" */
```

## API at a glance

| Header | Purpose |
|---|---|
| `cstdx/status.h` | `cx_status`, `cx_status_name` |
| `cstdx/checked.h` | `cx_add_overflow`, `cx_mul_overflow`, `cx_align_up`, `cx_align_ok` |
| `cstdx/allocator.h` | `cx_allocator` vtable + default allocator |
| `cstdx/arena.h`, `cstdx/pool.h` | Bump and fixed-slot allocators |
| `cstdx/allocator_counted.h`, `cstdx/allocator_canary.h` | Diagnostic decorators |
| `cstdx/string.h` | `cx_string`, an owned NUL-terminated byte string |
| `cstdx/string_view.h` | `cx_string_view`, a borrowed `(data, length)` pair |
| `cstdx/string_builder.h` | `cx_string_builder`, append + `printf` |
| `cstdx/bytes.h` | `cx_bytes`, an owned growable byte buffer |
| `cstdx/slice.h` | `cx_slice`, a borrowed byte range |
| `cstdx/vector.h` | `cx_vector`, a type-erased dynamic array |
| `cstdx/hash.h` | FNV-1a 32/64 and incremental `cx_hasher` |
| `cstdx/sort.h` | Stable bottom-up merge sort over `void * + element_size` |
| `cstdx/base64.h` | Strict RFC 4648 encode/decode |
| `cstdx/version.h` | Version string, numeric components, and git hash |

### Opaque owned types

`cx_string`, `cx_bytes`, `cx_vector`, and `cx_string_builder` are opaque
handles. Their layout is private to the library, so callers cannot accidentally
break the length/capacity invariants. Create them with `_init`/`_from`/`_copy`
and release them with `_destroy`:

```c
#include <cstdx/string.h>

cx_string *name = nullptr;

if (cx_string_from(&name, "hello", nullptr) != CX_OK) {
    return 1;
}

/* use name */
cx_string_destroy(&name);   /* name == nullptr now */
```

`_destroy` takes `cx_string **`, frees the control block, and sets the caller's
pointer to `nullptr`. Passing `NULL`, or a pointer to `NULL`, is a no-op, so a
second destroy is safe.

The allocator passed to `_init` owns both the handle and its buffer. A `NULL`
allocator means `cx_allocator_default()`.

`cx_allocator`, `cx_hasher`, `cx_arena`, `cx_pool`, and the decorators stay
visible: they are value types or extension points that must be stack-allocated
or returned by value.

### Errors

Functions return `cx_status` by value and write results through out-parameters.
`CX_OK` is zero. There are no exceptions, no global error state, and no error
strings in the library. Factories set `*out = nullptr` on failure.

## Testing

- `tests/c` — dependency-free C unit tests (17 executables).
- `tests/cpp` — differential/property tests against the STL
  (`std::string`, `std::vector`, `std::string_view`) using GoogleTest and
  RapidCheck. Enable with `-DCSTDX_BUILD_CPP_TESTS=ON`.
- `tests/fuzz` — libFuzzer targets for base64, strings, and vectors. Enable
  with `-DCSTDX_BUILD_FUZZ=ON` and `CMAKE_C_COMPILER=clang`.

The suite is clean under ASan and UBSan. The counting allocator asserts that
every allocating test returns to zero live allocations.

## Design documents

- [`docs/contract.md`](docs/contract.md) — ownership, allocator, aliasing, and
  error contracts.
- [`docs/differences.md`](docs/differences.md) — deliberate differences from
  the C++ standard library.

## License

See the repository `LICENSE`.
