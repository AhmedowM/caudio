#ifndef CSTDX_HASH_H
#define CSTDX_HASH_H

/**
 * @file hash.h
 * @brief Byte hash functions and an incremental FNV-1a hasher.
 *
 * The default algorithm is FNV-1a, chosen for its simplicity and stable
 * output. It is not cryptographic. Callers that need a different algorithm
 * can use the same incremental interface shape with their own implementation.
 */

#include <cstdx/api.h>
#include <cstdx/slice.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** FNV-1a 32-bit offset basis. */
static constexpr uint32_t CX_HASH_FNV1A_32_OFFSET = UINT32_C(2166136261);

/** FNV-1a 32-bit prime. */
static constexpr uint32_t CX_HASH_FNV1A_32_PRIME = UINT32_C(16777619);

/** FNV-1a 64-bit offset basis. */
static constexpr uint64_t CX_HASH_FNV1A_64_OFFSET = UINT64_C(14695981039346656037);

/** FNV-1a 64-bit prime. */
static constexpr uint64_t CX_HASH_FNV1A_64_PRIME = UINT64_C(1099511628211);

/**
 * @brief Hash @p length bytes with FNV-1a, 32-bit output.
 *
 * A null @p data pointer is treated as an empty input.
 */
[[nodiscard]] CX_API uint32_t cx_hash_fnv1a_32(const void* data, size_t length);

/**
 * @brief Hash @p length bytes with FNV-1a, 64-bit output.
 *
 * A null @p data pointer is treated as an empty input.
 */
[[nodiscard]] CX_API uint64_t cx_hash_fnv1a_64(const void* data, size_t length);

/**
 * @brief Incremental FNV-1a state.
 *
 * Treat @ref state as opaque.
 */
typedef struct cx_hasher {
    uint64_t state;
} cx_hasher;

/**
 * @brief Seed an incremental 64-bit FNV-1a hasher.
 *
 * @param hasher Storage to initialize.
 * @param seed Extra seed mixed into the standard offset basis. Pass zero for
 *             the standard FNV-1a result.
 */
CX_API void cx_hasher_init(cx_hasher* hasher, uint64_t seed);

/**
 * @brief Feed bytes into an incremental hasher.
 *
 * A null @p data pointer is treated as an empty input.
 */
CX_API void cx_hasher_update(cx_hasher* hasher, const void* data, size_t length);

/**
 * @brief Read the current 64-bit hash.
 */
[[nodiscard]] CX_API uint64_t cx_hasher_finish(const cx_hasher* hasher);

/**
 * @brief Hash a slice with a seed.
 *
 * Equivalent to @ref cx_hasher_init with @p seed followed by
 * @ref cx_hasher_update over @p data.
 */
[[nodiscard]] CX_API uint64_t cx_hash_seeded(cx_slice data, uint64_t seed);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_HASH_H */
