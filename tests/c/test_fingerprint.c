#include <caudio/media/fingerprint.h>

#include <caudio/runtime/error.h>
#include <caudio/runtime/fs.h>

#include <cstdx/allocator.h>

#include <stdio.h>
#include <string.h>

#include "ca_test.h"

static const char *TEST_FILE = "caudio_test_fingerprint_data.bin";

static void create_test_file(const char *path, const uint8_t *data, size_t len) {
    ca_error err;
    ca_error_init(&err);
    ca_file_t f = ca_fs_open(path, CA_FS_WRITE | CA_FS_CREATE | CA_FS_TRUNCATE, &err);
    CA_CHECK(f != nullptr);
    if (f) {
        ca_fs_write(f, data, len, &err);
        ca_fs_close(f);
    }
}

static void test_fingerprint_basic(void) {
    const uint8_t data[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    create_test_file(TEST_FILE, data, sizeof(data));

    ca_error err;
    ca_error_init(&err);
    uint8_t fp[32];
    CA_CHECK_EQ(CA_OK, ca_media_fingerprint_compute(TEST_FILE, fp, &err));

    /* Deterministic: same input produces same output */
    uint8_t fp2[32];
    CA_CHECK_EQ(CA_OK, ca_media_fingerprint_compute(TEST_FILE, fp2, &err));
    CA_CHECK(memcmp(fp, fp2, 32) == 0);

    /* Not all zeros */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) {
        if (fp[i] != 0) { all_zero = 0; break; }
    }
    CA_CHECK(!all_zero);

    ca_fs_remove(TEST_FILE, &err);
}

static void test_fingerprint_errors(void) {
    ca_error err;
    ca_error_init(&err);
    uint8_t fp[32];

    /* Empty path */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_media_fingerprint_compute("", fp, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_media_fingerprint_compute(nullptr, fp, &err));

    /* NULL out */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_media_fingerprint_compute(TEST_FILE, nullptr, &err));

    /* Nonexistent file */
    CA_CHECK_EQ(CA_IO_ERROR, ca_media_fingerprint_compute("caudio_nonexistent_fingerprint_file.bin", fp, &err));
}

static void test_fingerprint_different_inputs(void) {
    ca_error err;
    ca_error_init(&err);

    const uint8_t data1[] = {0xaa, 0xbb, 0xcc};
    const uint8_t data2[] = {0xaa, 0xbb, 0xcc, 0xdd};

    create_test_file("caudio_test_fp1.bin", data1, sizeof(data1));
    create_test_file("caudio_test_fp2.bin", data2, sizeof(data2));

    uint8_t fp1[32], fp2[32];
    CA_CHECK_EQ(CA_OK, ca_media_fingerprint_compute("caudio_test_fp1.bin", fp1, &err));
    CA_CHECK_EQ(CA_OK, ca_media_fingerprint_compute("caudio_test_fp2.bin", fp2, &err));
    CA_CHECK(memcmp(fp1, fp2, 32) != 0);

    ca_fs_remove("caudio_test_fp1.bin", &err);
    ca_fs_remove("caudio_test_fp2.bin", &err);
}

int main(void) {
    test_fingerprint_basic();
    test_fingerprint_errors();
    test_fingerprint_different_inputs();
    return ca_test_finish("fingerprint");
}
