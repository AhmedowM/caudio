#include <cstdx/hash.h>
#include <string.h>

#include "cx_test.h"

static int test_known_vectors(void) {
    CX_CHECK_EQ(UINT32_C(0x811c9dc5), cx_hash_fnv1a_32(nullptr, 0));
    CX_CHECK_EQ(UINT32_C(0x811c9dc5), cx_hash_fnv1a_32("", 0));
    CX_CHECK_EQ(UINT32_C(0xe40c292c), cx_hash_fnv1a_32("a", 1));
    CX_CHECK_EQ(UINT32_C(0xbf9cf968), cx_hash_fnv1a_32("foobar", 6));

    CX_CHECK(cx_hash_fnv1a_64(nullptr, 0) == UINT64_C(0xcbf29ce484222325));
    CX_CHECK(cx_hash_fnv1a_64("", 0) == UINT64_C(0xcbf29ce484222325));
    CX_CHECK(cx_hash_fnv1a_64("a", 1) == UINT64_C(0xaf63dc4c8601ec8c));
    CX_CHECK(cx_hash_fnv1a_64("foobar", 6) == UINT64_C(0x85944171f73967e8));

    return cx_test_finish("hash.known_vectors");
}

static int test_incremental(void) {
    cx_hasher one_shot;
    cx_hasher split;

    cx_hasher_init(&one_shot, 0);
    cx_hasher_update(&one_shot, "foobar", 6);

    cx_hasher_init(&split, 0);
    cx_hasher_update(&split, "foo", 3);
    cx_hasher_update(&split, nullptr, 0);
    cx_hasher_update(&split, "bar", 3);

    CX_CHECK(cx_hash_fnv1a_64("foobar", 6) == cx_hasher_finish(&one_shot));
    CX_CHECK(cx_hasher_finish(&one_shot) == cx_hasher_finish(&split));
    CX_CHECK(cx_hash_fnv1a_64("foobar", 6) == cx_hash_seeded(cx_slice_from_parts("foobar", 6), 0));

    CX_CHECK(CX_HASH_FNV1A_64_OFFSET == cx_hasher_finish(nullptr));
    CX_CHECK(CX_HASH_FNV1A_64_OFFSET == cx_hash_seeded((cx_slice){}, 0));

    cx_hasher seeded;

    cx_hasher_init(&seeded, 1234);
    cx_hasher_update(&seeded, "foobar", 6);
    CX_CHECK(cx_hasher_finish(&seeded) == cx_hash_seeded(cx_slice_from_parts("foobar", 6), 1234));
    CX_CHECK(cx_hasher_finish(&seeded) != cx_hash_fnv1a_64("foobar", 6));

    cx_hasher_init(nullptr, 0);
    cx_hasher_update(nullptr, "x", 1);

    return cx_test_finish("hash.incremental");
}

int main(void) {
    int failures = 0;

    failures += test_known_vectors();
    failures += test_incremental();

    return failures == 0 ? 0 : 1;
}
