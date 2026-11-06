# Changelog

All notable changes to `cstdx` are recorded here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow `MAJOR.MINOR.PATCH`. The ABI is not stable before 1.0.

## [Unreleased]

### Added

- Status codes (`cx_status`, `cx_status_name`) and checked arithmetic
  (`cx_add_overflow`, `cx_mul_overflow`, `cx_align_up`, `cx_align_ok`).
- Allocator vtable and default CRT allocator, plus `cx_arena` and `cx_pool`
  with static backing and mark/rewind/reset.
- Diagnostic allocator decorators: counting and canary. Failing and null
  allocators are test-only.
- Owned text: `cx_string`, `cx_string_view`, `cx_string_builder`.
- Binary: `cx_bytes` and `cx_slice`.
- Type-erased `cx_vector` with `cx_vector_ops`, overflow-checked growth,
  aliasing-safe insertion, and typed call-site macros.
- FNV-1a 32/64 hashing and incremental `cx_hasher`.
- Stable bottom-up merge sort over `void * + element_size`.
- Strict RFC 4648 base64 encode/decode with whitespace tolerance and canonical
  trailing-bit checks.
- C++23 differential/property tests (GoogleTest + RapidCheck) for string,
  bytes, vector, and base64.
- libFuzzer targets for base64, string, and vector.
- Standalone install/export as `cstdx::cstdx`.

### Changed

- `cx_string`, `cx_bytes`, `cx_vector`, and `cx_string_builder` are now opaque
  handles. `_init` allocates the control block and writes `cx_<type> **out`;
  `_destroy` frees it and sets the caller's pointer to `nullptr`.
- `cx_base64_encode`/`cx_base64_decode` now return owning handles.
- `cx_string_move`/`cx_bytes_move` move contents between existing handles and
  leave the source valid and empty.
- Typed vector accessors moved out of line; `cx_vector_data` and
  `cx_vector_data_const` are now the supported macros.

### Removed

- Legacy `byte_array` and `container` interfaces.
- Public field access for owned strings, bytes, vectors, and builders.

## [0.1.0] - TBD

First `cstdx` release. The feature set above is the v0.1 scope.
