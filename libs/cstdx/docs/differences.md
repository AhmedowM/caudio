# Differences from the C++ standard library

`cstdx` is not an STL clone. It is a C23 runtime with a deliberately smaller
surface. The C++ tests use `std::string`, `std::vector`, and `std::span` as
oracles, but the APIs differ in ways that matter.

## Types and construction

| `cstdx` | Closest STL type | Difference |
|---|---|---|
| `cx_string` | `std::string` | Opaque heap handle created with `cx_string_init`/`_from`/`_copy`; not a value type, not copyable by assignment. |
| `cx_string_view` | `std::string_view` | Plain `(data, length)` struct; no methods. Operations are free functions. |
| `cx_string_builder` | `std::ostringstream` | Append-only facade; `finish` transfers a new `cx_string` out. |
| `cx_bytes` | `std::vector<uint8_t>` | Opaque handle; optimized for byte buffers; `resize` zero-fills. |
| `cx_slice` | `std::span<const uint8_t>` | Borrowed `(data, length)`; no element type tracking. |
| `cx_vector` | `std::vector<T>` | Type-erased; element type described at runtime by size/alignment/callbacks. No templates in the ABI. |

There is no `cx_iterator`. Ranges are represented by slices or by
`(pointer, count)` pairs. Algorithms take `void * + element_size` rather than
iterators.

## Ownership and lifetime

- Owned objects are explicit handles. The handle's address is not the object's
  address after a move, and the handle is freed by `_destroy`.
- `_destroy` takes `cx_string **` and nulls the caller's pointer, making a
  double destroy a no-op.
- Copies are always deep and explicit (`cx_string_copy`, `cx_bytes_copy`).
  There is no copy constructor or assignment operator.
- `_move(dst, src)` moves between two existing handles and leaves `src` empty
  but valid. It is not a bitwise move of an owning record.
- Views and slices never own memory. Their lifetime is the caller's
  responsibility.

## Errors

- Errors are `cx_status` values, not exceptions. `CX_OK == 0`.
- Results are out-parameters. There is no `std::optional` or `std::expected`
  in the ABI.
- Invalid arguments are reported, not asserted. The library never throws,
  aborts, or terminates.
- Allocation failure is `CX_OUT_OF_MEMORY`; arithmetic overflow is
  `CX_OVERFLOW`; malformed base64 is `CX_INVALID_ARGUMENT`.

## Allocators

- Every allocating API takes `const cx_allocator *`; `nullptr` means the
  default allocator. There is no global `operator new` override and no
  polymorphic allocator hierarchy.
- The allocator receives the size and alignment at both allocation and free.
- Owned handles allocate their control block from the same allocator as their
  buffer.
- Arenas and pools expose `cx_allocator` views, but the view borrows the parent
  object and must not outlive it.
- Decorators (counted, canary) are stack-allocated value types wrapped around a
  parent allocator.

## Containers

- `cx_vector` element callbacks are a vtable (`cx_vector_ops`) rather than
  templates. `ops == nullptr` means trivially copyable.
- Growth doubles capacity with a minimum of 8 elements; `std::vector` growth is
  implementation-defined.
- `cx_vector_remove` preserves order like `std::vector::erase`.
  `cx_vector_swap_remove` is the unordered removal and is the cheaper default
  when order does not matter.
- `cx_vector_pop` writes the removed element through an out-parameter, or
  destroys it when the out-parameter is `nullptr`.
- `cx_vector_at` is a macro that type-checks the element size at runtime and
  returns a value, not a reference. Use `cx_vector_at_mut` for mutation.
- There is no `cx_map`, `cx_set`, `cx_deque`, `cx_list`, or `cx_smallvec` in
  v0.1.

## Strings

- `cx_string` may contain embedded NUL bytes; `length` is authoritative.
  `cx_string_cstr` returns a NUL-terminated view that truncates at the first
  embedded NUL.
- `cx_string_cstr(nullptr)` returns `""`, which mirrors `std::string_view`'s
  convenience but not `std::string` (which has no null state).
- String operations return `cx_status`; there are no `operator+`, `operator==`,
  or `substr` methods. Comparisons are explicit functions returning
  `CX_STRING_LESS`, `CX_STRING_EQUAL`, or `CX_STRING_GREATER`.
- Aliasing is explicitly supported: appending, inserting, or replacing with a
  view of the destination behaves as if the source were copied first.

## Algorithms

### Hashing

- `cx_hash` is FNV-1a 32/64, not `std::hash`. `std::hash` is intentionally
  unspecified and not stable across implementations; FNV-1a is stable and
  documented.
- `cx_hasher` is a visible value type for incremental hashing. It is not a
  streaming `std::hash` equivalent.

### Sorting

- `cx_sort` is always **stable** (bottom-up merge sort). `std::sort` is not
  stable; the closest STL equivalent is `std::stable_sort`.
- Sorting operates on `void *` plus `element_size`/`element_align` and takes a
  comparison function. It may allocate scratch from the supplied allocator.
- `cx_sort_array` and `cx_sort_vector` are typed convenience wrappers.

### Base64

- Decoding is deliberately **stricter** than many decoders:
  - invalid characters are rejected,
  - more than two `=` or data after `=` is rejected,
  - impossible remainder/padding combinations are rejected,
  - non-zero unused trailing bits are rejected (only canonical encodings are
    accepted).
- ASCII whitespace is ignored on decode.
- Padding is optional on decode and always emitted on encode.

## Public headers

- Every public header is valid C23 **and** C++23, with `extern "C"` guards.
- C-only constructs are not used in public headers.
- Structs that hold owned state are forward-declared only; their definitions
  live in the implementation. `cx_allocator`, `cx_hasher`, `cx_arena`,
  `cx_pool`, and the decorators remain visible because they are value types or
  extension points.
- There are no public inline functions that depend on private layout.

## What is intentionally absent

- Templates, exceptions, RTTI, and C++ in the shipped library.
- Iterators and ranges beyond borrowed slices/views.
- A container base interface or runtime polymorphic container hierarchy.
- `printf`-style formatting beyond `cx_string_builder_printf`.
- Locale-sensitive parsing or formatting.
- Thread synchronization inside containers.
