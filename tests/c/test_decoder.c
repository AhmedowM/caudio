#include <caudio/media/decoder.h>
#include <caudio/media/reader.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>
#include <cstdx/string_view.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ca_test.h"

/* Mirror kanariv tests/test_decoder.cpp: probe logic with mocked header bytes,
 * and open/seek validation. The garbage-bytes open uses the real FFmpeg
 * backend (headless-safe; FFmpeg is a required dependency, not a stub). */

static void test_decoder_probe(void) {
    /* 4+ bytes -> a decode attempt should proceed */
    uint8_t data4[] = {0x00, 0x01, 0x02, 0x03};
    cx_string_view v4 =
        cx_string_view_from_parts((const char *)data4, sizeof(data4));
    CA_CHECK(ca_decoder_probe(v4));

    uint8_t data8[] = {0xFF, 0xFB, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00};
    cx_string_view v8 =
        cx_string_view_from_parts((const char *)data8, sizeof(data8));
    CA_CHECK(ca_decoder_probe(v8));

    /* < 4 bytes -> do not attempt */
    uint8_t data3[] = {0x00, 0x01, 0x02};
    cx_string_view v3 =
        cx_string_view_from_parts((const char *)data3, sizeof(data3));
    CA_CHECK(!ca_decoder_probe(v3));

    uint8_t data1[] = {0x00};
    cx_string_view v1 =
        cx_string_view_from_parts((const char *)data1, sizeof(data1));
    CA_CHECK(!ca_decoder_probe(v1));
}

static void test_decoder_open_validation(void) {
    ca_error err;
    ca_decoder *d = nullptr;
    ca_reader *r = nullptr;

    ca_error_init(&err);
    CA_CHECK_EQ(CA_OK, ca_reader_memory(&r, (const uint8_t *)"abcd", 4, &err));
    CA_CHECK(r != nullptr);

    /* NULL out handle */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_decoder_open(nullptr, r, nullptr, &err));

    /* NULL reader */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_decoder_open(&d, nullptr, nullptr, &err));
    CA_CHECK(d == nullptr);

    ca_reader_destroy(r);
}

static void test_decoder_seek_validation(void) {
    ca_error err;
    ca_decoder *d = nullptr; /* never opened -> all seeks reject */

    /* NULL decoder */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_decoder_seek(nullptr, 1.0, &err));

    /* invalid seconds */
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_decoder_seek(d, -1.0, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_decoder_seek(d, NAN, &err));
    ca_error_init(&err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_decoder_seek(d, INFINITY, &err));
}

int main(void) {
    test_decoder_probe();
    test_decoder_open_validation();
    test_decoder_seek_validation();
    return ca_test_finish("decoder");
}
