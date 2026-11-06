#ifndef CSTDX_VECTOR_H
#define CSTDX_VECTOR_H

/**
 * @file vector.h
 * @brief Type-erased dynamic vector (cx_vector).
 *
 * The element type is described at construction time by a size, an alignment,
 * and optional copy/move/destroy callbacks. One implementation therefore
 * serves every element type, including user-defined structs.
 *
 * `ops == nullptr` means the element type is trivially copyable: copy and move
 * use `memcpy` and destroy does nothing. When `ops != nullptr`, @ref
 * cx_vector_ops::copy and @ref cx_vector_ops::destroy are required; @ref
 * cx_vector_ops::move may be nullptr, in which case copy is used.
 *
 * Element types must be complete, non-array object types. Use a struct to
 * group compound values. `element_size` must be a multiple of
 * `element_align`, as required by C for any real object type.
 */

#include <stddef.h>
#include <stdint.h>

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/slice.h>
#include <cstdx/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Element lifecycle callbacks.
 *
 * Each callback receives pointers to elements whose concrete type is known to
 * the caller that supplied the callbacks.
 */
typedef struct cx_vector_ops {
    /** Copy one element into uninitialized storage. Required when ops != null. */
    void (*copy)(void *dst, const void *src);

    /** Move one element into uninitialized storage, leaving @p src destructible. */
    void (*move)(void *dst, void *src);

    /** Destroy one element in place. Required when ops != null. */
    void (*destroy)(void *element);
} cx_vector_ops;

/**
 * @brief A type-erased dynamic vector.
 *
 * This is an opaque handle. Its layout is private to the library. Create one
 * with @ref cx_vector_init and release it with @ref cx_vector_destroy.
 */
typedef struct cx_vector cx_vector;

/**
 * @brief Initialize an empty vector.
 *
 * @param[out] out Receives a newly allocated handle on success. Set to
 *                 nullptr on failure.
 * @param element_size Size of one element in bytes; must be non-zero.
 * @param element_align Alignment of the element type; must be a power of two.
 * @param ops Lifecycle callbacks, or nullptr for trivially copyable elements.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 *                  The same allocator owns the handle and the storage.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_MEMORY`.
 */
[[nodiscard]] CX_API cx_status cx_vector_init(
    cx_vector **out, size_t element_size, size_t element_align,
    const cx_vector_ops *ops, const cx_allocator *allocator);

/**
 * @brief Destroy all elements, release storage, and the handle itself.
 *
 * Sets @p out to nullptr. Passing nullptr, or a pointer to nullptr, is a
 * no-op. A second destroy of the same variable is therefore safe.
 */
CX_API void cx_vector_destroy(cx_vector **out);

/**
 * @brief Ensure capacity for at least @p capacity elements.
 *
 * The length is unchanged. Existing elements may move, so pointers into the
 * vector are invalidated.
 */
[[nodiscard]] CX_API cx_status cx_vector_reserve(cx_vector *vector,
                                                 size_t capacity);

/**
 * @brief Release unused capacity.
 */
[[nodiscard]] CX_API cx_status cx_vector_shrink_to_fit(cx_vector *vector);

/**
 * @brief Copy @p element onto the end.
 *
 * @p element_size must equal the vector's element size. Aliasing is supported:
 * @p element may point into the vector's own storage.
 */
[[nodiscard]] CX_API cx_status cx_vector_push_copy(cx_vector *vector,
                                                   const void *element,
                                                   size_t element_size);

/**
 * @brief Copy @p element into position @p index.
 *
 * @p index may equal the current length. Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_vector_insert_copy(cx_vector *vector,
                                                     size_t index,
                                                     const void *element,
                                                     size_t element_size);

/**
 * @brief Move the last element into @p out and remove it.
 *
 * @param out Destination for the element, or nullptr to destroy it.
 *            @p out must not point into the vector's storage.
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_RANGE` when empty.
 */
[[nodiscard]] CX_API cx_status cx_vector_pop(cx_vector *vector, void *out);

/**
 * @brief Remove the element at @p index, preserving order.
 */
[[nodiscard]] CX_API cx_status cx_vector_remove(cx_vector *vector,
                                                size_t index);

/**
 * @brief Remove the element at @p index by moving the last element into it.
 *
 * Does not preserve order.
 */
[[nodiscard]] CX_API cx_status cx_vector_swap_remove(cx_vector *vector,
                                                     size_t index);

/**
 * @brief Destroy all elements and keep the capacity.
 */
CX_API void cx_vector_clear(cx_vector *vector);

/**
 * @brief Pointer to element @p index, or nullptr when out of range.
 */
[[nodiscard]] CX_API const void *cx_vector_ptr(const cx_vector *vector,
                                               size_t index);

/**
 * @brief Mutable pointer to element @p index, or nullptr when out of range.
 */
[[nodiscard]] CX_API void *cx_vector_ptr_mut(cx_vector *vector, size_t index);

/**
 * @brief Number of live elements.
 */
[[nodiscard]] CX_API size_t cx_vector_length(const cx_vector *vector);

/**
 * @brief Number of elements that fit without reallocating.
 */
[[nodiscard]] CX_API size_t cx_vector_capacity(const cx_vector *vector);

/**
 * @brief Element size in bytes.
 */
[[nodiscard]] CX_API size_t cx_vector_element_size(const cx_vector *vector);

/**
 * @brief True when the vector is null or has no elements.
 */
[[nodiscard]] CX_API bool cx_vector_empty(const cx_vector *vector);

/**
 * @brief Borrow the live elements as a byte slice.
 */
[[nodiscard]] CX_API cx_slice cx_vector_as_slice(const cx_vector *vector);

/**
 * @brief Alignment of one element.
 */
[[nodiscard]] CX_API size_t cx_vector_element_align(const cx_vector *vector);

/**
 * @brief Mutable pointer to the first element, or nullptr when unallocated.
 */
[[nodiscard]] CX_API void *cx_vector_data_ptr(cx_vector *vector);

/**
 * @brief Read-only pointer to the first element, or nullptr when unallocated.
 */
[[nodiscard]] CX_API const void *cx_vector_data_ptr_const(
    const cx_vector *vector);

/**
 * @brief Bounds- and type-checked pointer access for header macros.
 *
 * Returns nullptr when @p vector is null, @p element_size does not match, or
 * @p index is out of range.
 */
[[nodiscard]] CX_API const void *cx_vector_ptr_typed(
    const cx_vector *vector, size_t index, size_t element_size);

/**
 * @brief Mutable counterpart of @ref cx_vector_ptr_typed.
 */
[[nodiscard]] CX_API void *cx_vector_ptr_mut_typed(
    cx_vector *vector, size_t index, size_t element_size);

/**
 * @brief Initialize a vector for element type @p T.
 */
#define cx_vector_init_of(vector, T, ops, allocator) \
    cx_vector_init((vector), sizeof(T), alignof(T), (ops), (allocator))

/**
 * @brief Push the value pointed to by @p pointer.
 */
#define cx_vector_push(vector, pointer) \
    cx_vector_push_copy((vector), (pointer), sizeof(*(pointer)))

/**
 * @brief Push an lvalue.
 */
#define cx_vector_push_value(vector, lvalue) \
    cx_vector_push_copy((vector), &(lvalue), sizeof(lvalue))

/**
 * @brief Read element @p index as type @p T.
 */
#define cx_vector_at(vector, T, index) \
    (*(const T *)cx_vector_ptr_typed((vector), (index), sizeof(T)))

/**
 * @brief Mutable access to element @p index as type @p T.
 */
#define cx_vector_at_mut(vector, T, index) \
    (*(T *)cx_vector_ptr_mut_typed((vector), (index), sizeof(T)))

/**
 * @brief Typed pointer to the first element.
 */
#define cx_vector_data(vector, T) ((T *)cx_vector_data_ptr(vector))

/**
 * @brief Read-only typed pointer to the first element.
 */
#define cx_vector_data_const(vector, T) \
    ((const T *)cx_vector_data_ptr_const(vector))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_VECTOR_H */
