#include <cstdx/hash.h>

static uint32_t fnv1a_32_update(uint32_t state, const uint8_t *data,
                                size_t length) {
    for (size_t i = 0; i < length; ++i) {
        state ^= data[i];
        state *= CX_HASH_FNV1A_32_PRIME;
    }

    return state;
}

static uint64_t fnv1a_64_update(uint64_t state, const uint8_t *data,
                                size_t length) {
    for (size_t i = 0; i < length; ++i) {
        state ^= data[i];
        state *= CX_HASH_FNV1A_64_PRIME;
    }

    return state;
}

uint32_t cx_hash_fnv1a_32(const void *data, size_t length) {
    if (data == nullptr) {
        length = 0;
    }

    return fnv1a_32_update(CX_HASH_FNV1A_32_OFFSET, (const uint8_t *)data,
                           length);
}

uint64_t cx_hash_fnv1a_64(const void *data, size_t length) {
    if (data == nullptr) {
        length = 0;
    }

    return fnv1a_64_update(CX_HASH_FNV1A_64_OFFSET, (const uint8_t *)data,
                           length);
}

void cx_hasher_init(cx_hasher *hasher, uint64_t seed) {
    if (hasher == nullptr) {
        return;
    }

    hasher->state = CX_HASH_FNV1A_64_OFFSET ^ seed;
}

void cx_hasher_update(cx_hasher *hasher, const void *data, size_t length) {
    if (hasher == nullptr || data == nullptr || length == 0) {
        return;
    }

    hasher->state = fnv1a_64_update(hasher->state, (const uint8_t *)data,
                                    length);
}

uint64_t cx_hasher_finish(const cx_hasher *hasher) {
    return hasher != nullptr ? hasher->state : CX_HASH_FNV1A_64_OFFSET;
}

uint64_t cx_hash_seeded(cx_slice data, uint64_t seed) {
    cx_hasher hasher;

    cx_hasher_init(&hasher, seed);
    cx_hasher_update(&hasher, data.data, data.length);
    return cx_hasher_finish(&hasher);
}
