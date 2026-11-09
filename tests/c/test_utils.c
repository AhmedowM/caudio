#include <caudio/utils/math.h>
#include <caudio/utils/print.h>
#include <caudio/utils/ring.h>
#include <caudio/utils/mpsc.h>

#include <cstdx/allocator.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "ca_test.h"

/* ---------------------------------------------------------- */
/* math: ca_clamp_volume                                      */
/* ---------------------------------------------------------- */

static void test_clamp_volume(void) {
    CA_CHECK_EQ(0.0f, ca_clamp_volume(-1.0f));
    CA_CHECK_EQ(0.0f, ca_clamp_volume(-0.5f));
    CA_CHECK_EQ(0.0f, ca_clamp_volume(0.0f));
    CA_CHECK_EQ(0.5f, ca_clamp_volume(0.5f));
    CA_CHECK_EQ(1.0f, ca_clamp_volume(1.0f));
    CA_CHECK_EQ(1.0f, ca_clamp_volume(2.0f));
    CA_CHECK_EQ(1.0f, ca_clamp_volume(100.0f));
    CA_CHECK_EQ(0.0f, ca_clamp_volume(NAN));
    CA_CHECK_EQ(0.0f, ca_clamp_volume(INFINITY));
    CA_CHECK_EQ(0.0f, ca_clamp_volume(-INFINITY));
}

/* ---------------------------------------------------------- */
/* math: ca_to_hex / ca_from_hex                              */
/* ---------------------------------------------------------- */

static void test_to_hex(void) {
    const cx_allocator* alloc = cx_allocator_default();
    cx_string* s = nullptr;

    /* 32 zero bytes -> 64 '0' chars */
    uint8_t zeros[32];
    memset(zeros, 0, sizeof(zeros));
    CA_CHECK_EQ(CA_OK, ca_to_hex(zeros, alloc, &s));
    CA_CHECK(s != nullptr);
    CA_CHECK(cx_string_length(s) == 64);
    CA_CHECK(cx_string_cstr(s)[0] == '0');
    cx_string_destroy(&s);

    uint8_t bytes[32];
    memset(bytes, 0, sizeof(bytes));
    bytes[0] = 0xab;
    bytes[1] = 0xcd;
    bytes[31] = 0xef;

    CA_CHECK_EQ(CA_OK, ca_to_hex(bytes, alloc, &s));
    CA_CHECK(s != nullptr);
    CA_CHECK(cx_string_length(s) == 64);
    CA_CHECK(cx_string_cstr(s)[0] == 'a');
    CA_CHECK(cx_string_cstr(s)[1] == 'b');
    CA_CHECK(cx_string_cstr(s)[2] == 'c');
    CA_CHECK(cx_string_cstr(s)[3] == 'd');
    CA_CHECK(cx_string_cstr(s)[62] == 'e');
    CA_CHECK(cx_string_cstr(s)[63] == 'f');
    cx_string_destroy(&s);
}

static void test_from_hex(void) {
    uint8_t out[32];
    const cx_allocator* alloc = cx_allocator_default();
    cx_string* hex = nullptr;

    /* Build a known 64-char hex string from bytes */
    uint8_t bytes[32];
    memset(bytes, 0, sizeof(bytes));
    bytes[0] = 0xab;
    bytes[1] = 0xcd;
    bytes[31] = 0xef;
    CA_CHECK_EQ(CA_OK, ca_to_hex(bytes, alloc, &hex));
    CA_CHECK(hex != nullptr);

    CA_CHECK(ca_from_hex(cx_string_view_of(hex), out));
    CA_CHECK(out[0] == 0xab);
    CA_CHECK(out[1] == 0xcd);
    CA_CHECK(out[31] == 0xef);
    cx_string_destroy(&hex);

    /* Case insensitive - use uppercase */
    CA_CHECK_EQ(CA_OK, ca_to_hex(bytes, alloc, &hex));
    CA_CHECK(hex != nullptr);
    /* Uppercase the hex string */
    char* p = cx_string_at_mut(hex, 0);
    for (size_t i = 0; i < cx_string_length(hex); i++) {
        p[i] = (char)toupper((unsigned char)p[i]);
    }
    CA_CHECK(ca_from_hex(cx_string_view_of(hex), out));
    CA_CHECK(out[0] == 0xab);
    CA_CHECK(out[1] == 0xcd);
    cx_string_destroy(&hex);

    /* Wrong length */
    CA_CHECK(!ca_from_hex(cx_string_view_from_cstr("abcd"), out));
    CA_CHECK(!ca_from_hex(cx_string_view_from_cstr(""), out));

    /* Invalid characters */
    CA_CHECK(!ca_from_hex(cx_string_view_from_cstr("zzcd0000000000000000000000000000000000000000000000000000000000"), out));
    CA_CHECK(!ca_from_hex(cx_string_view_from_cstr("abcd00000000000000000000000000000000000000000000000000000000ez"), out));

    /* NULL out */
    CA_CHECK(!ca_from_hex(cx_string_view_from_cstr("abcd00000000000000000000000000000000000000000000000000000000ef"), nullptr));
}

/* ---------------------------------------------------------- */
/* math: ca_format_clock                                      */
/* ---------------------------------------------------------- */

static void test_format_clock(void) {
    const cx_allocator* alloc = cx_allocator_default();
    cx_string* s = nullptr;

    CA_CHECK_EQ(CA_OK, ca_format_clock(0.0, alloc, &s));
    CA_CHECK(strcmp(cx_string_cstr(s), "00:00") == 0);
    cx_string_destroy(&s);

    CA_CHECK_EQ(CA_OK, ca_format_clock(5.0, alloc, &s));
    CA_CHECK(strcmp(cx_string_cstr(s), "00:05") == 0);
    cx_string_destroy(&s);

    CA_CHECK_EQ(CA_OK, ca_format_clock(65.0, alloc, &s));
    CA_CHECK(strcmp(cx_string_cstr(s), "01:05") == 0);
    cx_string_destroy(&s);

    CA_CHECK_EQ(CA_OK, ca_format_clock(3665.0, alloc, &s));
    CA_CHECK(strcmp(cx_string_cstr(s), "01:01:05") == 0);
    cx_string_destroy(&s);

    CA_CHECK_EQ(CA_OK, ca_format_clock(3600.0, alloc, &s));
    CA_CHECK(strcmp(cx_string_cstr(s), "01:00:00") == 0);
    cx_string_destroy(&s);

    CA_CHECK_EQ(CA_OK, ca_format_clock(-1.0, alloc, &s));
    CA_CHECK(strcmp(cx_string_cstr(s), "00:00") == 0);
    cx_string_destroy(&s);

    CA_CHECK_EQ(CA_OK, ca_format_clock(3599.999, alloc, &s));
    CA_CHECK(strcmp(cx_string_cstr(s), "59:59") == 0);
    cx_string_destroy(&s);
}

/* ---------------------------------------------------------- */
/* utils: ca_ring                                             */
/* ---------------------------------------------------------- */

static void test_ring_basic(void) {
    const cx_allocator* alloc = cx_allocator_default();
    ca_ring* ring = nullptr;
    CA_CHECK_EQ(CA_OK, ca_ring_create(&ring, 4, 2, alloc));
    CA_CHECK(ring != nullptr);
    CA_CHECK_EQ(4, ca_ring_capacity(ring));
    CA_CHECK_EQ(2, ca_ring_channels(ring));

    CA_CHECK_EQ(0, ca_ring_available_read(ring));
    CA_CHECK_EQ(4, ca_ring_available_write(ring));

    uint8_t write_buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CA_CHECK_EQ(4, ca_ring_write(ring, write_buf, 4));
    CA_CHECK_EQ(4, ca_ring_available_read(ring));
    CA_CHECK_EQ(0, ca_ring_available_write(ring));

    uint8_t read_buf[8];
    CA_CHECK_EQ(4, ca_ring_read(ring, read_buf, 4));
    CA_CHECK(memcmp(read_buf, write_buf, 8) == 0);
    CA_CHECK_EQ(0, ca_ring_available_read(ring));
    CA_CHECK_EQ(4, ca_ring_available_write(ring));

    ca_ring_destroy(ring);
}

static void test_ring_wrap(void) {
    const cx_allocator* alloc = cx_allocator_default();
    ca_ring* ring = nullptr;
    CA_CHECK_EQ(CA_OK, ca_ring_create(&ring, 4, 1, alloc));

    int8_t w1[4] = {1, 2, 3, 4};
    CA_CHECK_EQ(4, ca_ring_write(ring, w1, 4));
    /* Ring is full; second write returns 0 */
    int8_t w2[4] = {5, 6, 7, 8};
    CA_CHECK_EQ(0, ca_ring_write(ring, w2, 4));

    int8_t r1[4];
    CA_CHECK_EQ(4, ca_ring_read(ring, r1, 4));
    CA_CHECK(memcmp(r1, w1, 4) == 0);
    CA_CHECK_EQ(0, ca_ring_available_read(ring));
    CA_CHECK_EQ(4, ca_ring_available_write(ring));

    ca_ring_destroy(ring);
}

static void test_ring_partial(void) {
    const cx_allocator* alloc = cx_allocator_default();
    ca_ring* ring = nullptr;
    CA_CHECK_EQ(CA_OK, ca_ring_create(&ring, 4, 1, alloc));

    int8_t w[2] = {1, 2};
    CA_CHECK_EQ(2, ca_ring_write(ring, w, 2));

    int8_t r[1];
    CA_CHECK_EQ(1, ca_ring_read(ring, r, 1));
    CA_CHECK(r[0] == 1);

    int8_t w2[3] = {3, 4, 5};
    CA_CHECK_EQ(3, ca_ring_write(ring, w2, 3));

    int8_t r2[4];
    CA_CHECK_EQ(4, ca_ring_read(ring, r2, 4));
    CA_CHECK(r2[0] == 2);
    CA_CHECK(r2[1] == 3);
    CA_CHECK(r2[2] == 4);
    CA_CHECK(r2[3] == 5);

    ca_ring_destroy(ring);
}

static void test_ring_reset(void) {
    const cx_allocator* alloc = cx_allocator_default();
    ca_ring* ring = nullptr;
    CA_CHECK_EQ(CA_OK, ca_ring_create(&ring, 4, 1, alloc));

    int8_t w[4] = {1, 2, 3, 4};
    CA_CHECK_EQ(4, ca_ring_write(ring, w, 4));
    ca_ring_reset(ring);
    CA_CHECK_EQ(0, ca_ring_available_read(ring));
    CA_CHECK_EQ(4, ca_ring_available_write(ring));

    ca_ring_destroy(ring);
}

/* ---------------------------------------------------------- */
/* utils: ca_mpsc                                             */
/* ---------------------------------------------------------- */

static void test_mpsc_basic(void) {
    const cx_allocator* alloc = cx_allocator_default();
    ca_mpsc* q = nullptr;
    CA_CHECK_EQ(CA_OK, ca_mpsc_create(&q, 4, sizeof(int32_t), alloc));
    CA_CHECK(q != nullptr);
    CA_CHECK_EQ(4, ca_mpsc_capacity(q));
    CA_CHECK_EQ(0, ca_mpsc_size(q));
    CA_CHECK(ca_mpsc_empty(q));

    int32_t v1 = 42;
    CA_CHECK_EQ(CA_OK, ca_mpsc_push_copy(q, &v1));
    CA_CHECK_EQ(1, ca_mpsc_size(q));
    CA_CHECK(!ca_mpsc_empty(q));

    int32_t v2 = 0;
    CA_CHECK_EQ(CA_OK, ca_mpsc_pop(q, &v2));
    CA_CHECK_EQ(42, v2);
    CA_CHECK_EQ(0, ca_mpsc_size(q));
    CA_CHECK(ca_mpsc_empty(q));

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_mpsc_pop(q, &v2));

    ca_mpsc_destroy(q);
}

static void test_mpsc_full(void) {
    const cx_allocator* alloc = cx_allocator_default();
    ca_mpsc* q = nullptr;
    CA_CHECK_EQ(CA_OK, ca_mpsc_create(&q, 2, sizeof(int32_t), alloc));

    int32_t v1 = 1, v2 = 2;
    CA_CHECK_EQ(CA_OK, ca_mpsc_push_copy(q, &v1));
    CA_CHECK_EQ(CA_OK, ca_mpsc_push_copy(q, &v2));
    CA_CHECK_EQ(CA_OVERFLOW, ca_mpsc_push_copy(q, &v1));

    int32_t out = 0;
    CA_CHECK_EQ(CA_OK, ca_mpsc_pop(q, &out));
    CA_CHECK_EQ(1, out);
    CA_CHECK_EQ(CA_OK, ca_mpsc_push_copy(q, &v1));
    CA_CHECK_EQ(CA_OK, ca_mpsc_pop(q, &out));
    CA_CHECK_EQ(2, out);
    CA_CHECK_EQ(CA_OK, ca_mpsc_pop(q, &out));
    CA_CHECK_EQ(1, out);

    ca_mpsc_destroy(q);
}

/* ---------------------------------------------------------- */
/* main                                                        */
/* ---------------------------------------------------------- */

int main(void) {
    test_clamp_volume();
    test_to_hex();
    test_from_hex();
    test_format_clock();
    test_ring_basic();
    test_ring_wrap();
    test_ring_partial();
    test_ring_reset();
    test_mpsc_basic();
    test_mpsc_full();
    return ca_test_finish("utils");
}
