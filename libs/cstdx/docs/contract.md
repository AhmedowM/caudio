# cstdx contract

This document defines the invariants callers may rely on and the obligations
callers must satisfy. It applies to `cstdx` 0.1.x.

## 1. Status and errors

- Every fallible function returns `cx_status` by value.
- `CX_OK == 0`; all other values are errors. `cx_status` is a 1-byte enum with
  a fixed underlying type.
- Results are written through out-parameters, never through globals or
  `errno`.
- Factories that allocate a handle set `*out = nullptr` before doing work and
  leave it `nullptr` on failure.
- The library never aborts, throws, or exits on invalid input. Invalid
  arguments return `CX_INVALID_ARGUMENT`; allocation failure returns
  `CX_OUT_OF_MEMORY`; arithmetic overflow returns `CX_OVERFLOW`.
- `CX_INTERNAL` means a broken library invariant, not caller error. A caller
  should treat it as a bug and surface it.

## 2. Memory and allocators

### 2.1 The vtable

`cx_allocator` is a value type with `state`, `alloc`, `realloc`, and `free`.
`alloc` and `free` are required; `realloc` may be `nullptr`.

- `alignment` must be a nonzero power of two. `0` means
  `alignof(max_align_t)`.
- `size == 0` returns `nullptr` and is not an error.
- `free(nullptr)` is a no-op.
- `free` receives the same `size` and `alignment` used to allocate.
- `realloc(nullptr, old, new, align)` allocates.
- `realloc(ptr, old, 0, align)` frees and returns `nullptr`.
- A failed reallocation returns `nullptr` and leaves the original block valid.
- A block must be freed by the allocator that produced it.

### 2.2 Null means default

Every API that takes `const cx_allocator *` accepts `nullptr` to mean
`cx_allocator_default()`. This applies to handles, arenas, pools, decorators,
containers, base64, and sort scratch space.

### 2.3 Handle ownership

For `cx_string`, `cx_bytes`, `cx_vector`, and `cx_string_builder`:

- `_init`, `_from`, `_from_view`, `_from_slice`, and `_copy` allocate the
  control block and the backing storage from the supplied allocator.
- The same allocator owns both. The library never mixes allocators.
- `_destroy` frees the backing storage and the control block, then sets the
  caller's pointer to `nullptr`.
- `_destroy(NULL)` and `_destroy(&ptr_that_is_null)` are no-ops.
- A handle must not be freed with `free()` or by copying its bytes.
- `_move(dst, src)` requires two live handles. It releases `dst`'s current
  storage and transfers `src`'s contents into `dst`. `src` remains a valid,
  empty handle with its original allocator.
- Handle values are not stable across moves; do not store raw pointers beyond
  the lifetime of the handle.

### 2.4 Arenas and pools

`cx_arena` and `cx_pool` are visible structs because they support static
backing with no allocation:

- `cx_arena_init_static` and `cx_pool_init_static` perform no allocation.
- `cx_arena_as_allocator` and the pool allocator return `cx_allocator` by
  value. The returned vtable borrows the arena/pool; it must not outlive it.
- `cx_arena_reset`/`cx_arena_rewind` invalidate every allocation from the
  reset region. Pointers must not be used afterwards.

### 2.5 Decorators

`cx_allocator_counted` and `cx_allocator_canary` are visible value types. They
are initialized with `_init`, exposed with `_as_allocator`, and released with
`_destroy`. The wrapped allocator must outlive the decorator.

Failing and null allocators are test-only and live in
`tests/c/cx_test_allocators.h`.

## 3. Strings, views, slices, and bytes

### 3.1 `cx_string`

- NUL-terminated and may contain embedded NUL bytes; `length` is authoritative.
- Invariants: `capacity >= length`; when a buffer exists, `data[length] == '\0'`
  and the allocation is `capacity + 1` bytes.
- `cx_string_cstr` never returns `nullptr`; it returns `""` for a null handle.
- Growing operations may reallocate and invalidate pointers into the buffer.
- Aliasing is supported: `cx_string_append(s, s)`, inserting a view of `s` into
  `s`, and replacing with a view of `s` all behave as if the source were copied
  first.

### 3.2 `cx_string_view`

- A borrowed `(data, length)` pair. It owns nothing and is not guaranteed to be
  NUL-terminated.
- `data == nullptr` is only meaningful when `length == 0`.
- The caller must keep the underlying storage alive and unmodified for the
  lifetime of the view.

### 3.3 `cx_slice`

- A borrowed byte range with the same lifetime rules as `cx_string_view`.
- `cx_bytes_as_slice` and `cx_vector_as_slice` borrow live storage; any
  mutation that reallocates invalidates the slice.

### 3.4 `cx_bytes`

- Owns a growable byte buffer. `length` is the number of valid bytes;
  `capacity` is the allocation size in bytes.
- New bytes from `cx_bytes_resize` are zero-filled.
- Aliasing is supported for assign/append/insert/replace.

## 4. `cx_vector`

- Type-erased: `element_size` and `element_align` describe the element type.
  `element_size` must be nonzero; `element_align` must be a power of two.
- `ops == nullptr` means trivially copyable: moves/copies use `memcpy` and
  destroy is a no-op.
- If `ops != nullptr`, `ops->copy` and `ops->destroy` are required. `move` is
  optional; when absent, copy is used.
- `cx_vector_push_copy` and `cx_vector_insert_copy` accept an element pointer
  that aliases the vector's own storage. The element is copied before growth.
- `cx_vector_pop` moves the last element into `out`, or destroys it when `out`
  is `nullptr`. `out` must not point into the vector's storage.
- `cx_vector_remove` preserves order; `cx_vector_swap_remove` does not.
- Growth doubles capacity with a minimum of 8 elements and is
  overflow-checked.
- `cx_vector_shrink_to_fit` preserves the elements and may invalidate
  pointers.
- Element pointers are invalidated by any operation that changes capacity.

## 5. Hashing and sorting

- `cx_hash` is FNV-1a 32/64. A seed is XORed into the offset basis.
- `cx_hasher` is a visible value type and supports incremental updates.
- `cx_sort` is a stable bottom-up merge sort over
  `(void *data, size_t count, size_t element_size, size_t element_align)`.
- The comparison function must define a strict weak ordering.
- Sort scratch comes from the supplied allocator; `nullptr` means default.
- Sorting may allocate. It returns `CX_OUT_OF_MEMORY` or `CX_OVERFLOW` instead
  of sorting partially when scratch allocation fails.

## 6. Base64

- Standard RFC 4648 alphabet `A-Z a-z 0-9 + /` with `=` padding on encode.
- Decoding ignores ASCII whitespace (` `, `\t`, `\n`, `\r`, `\v`, `\f`).
- Decoding accepts input with or without padding.
- Decoding rejects:
  - characters outside the alphabet and whitespace,
  - more than two `=`,
  - data after the first `=`,
  - impossible remainder/padding combinations,
  - non-zero unused trailing bits (non-canonical encodings).
- `cx_base64_encode` and `cx_base64_decode` return newly allocated handles.

## 7. Threading

- The default allocator is thread-safe.
- Decorators, arenas, pools, strings, bytes, vectors, and builders are **not**
  synchronized. A single object must not be used concurrently by multiple
  threads without external locking.
- Distinct objects backed by distinct allocators are independent.
- `cx_hash` and `cx_sort` operate on caller-provided memory and add no shared
  state.

## 8. Compatibility

- Public headers are valid C23 and C++23 and are wrapped in `extern "C"`.
- The pre-1.0 ABI is not stable. Source compatibility is maintained within a
  minor series where practical; opaque handles are used so field layout is not
  part of the contract.
- `cstdx` never includes a caudio header and never links C++.
