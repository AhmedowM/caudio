#include <caudio/media/reader.h>
#include <caudio/media/source.h>

#include <caudio/runtime/error.h>
#include <caudio/runtime/fs.h>

#include <cstdx/allocator.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "ca_test.h"

static const char *TEST_FILE = "caudio_test_source_data.bin";

static void create_test_file(const char *path, const char *data, size_t len) {
    ca_error err;
    ca_error_init(&err);
    ca_file_t f = ca_fs_open(path, CA_FS_WRITE | CA_FS_CREATE | CA_FS_TRUNCATE, &err);
    CA_CHECK(f != nullptr);
    if (f) {
        ca_fs_write(f, data, len, &err);
        ca_fs_close(f);
    }
}

static void test_path_source(void) {
    ca_error err;
    ca_error_init(&err);
    create_test_file(TEST_FILE, "test data", 9);

    ca_media_source *src = nullptr;
    CA_CHECK_EQ(CA_OK, ca_media_source_path(&src, TEST_FILE, &err));
    CA_CHECK(src != nullptr);

    CA_CHECK_EQ(CA_MEDIA_SOURCE_PATH, ca_media_source_kind(src));
    CA_CHECK(strcmp(ca_media_source_display_name(src), TEST_FILE) == 0);
    CA_CHECK(ca_media_source_seekable(src));
    CA_CHECK(strcmp(ca_media_source_path_path(src), TEST_FILE) == 0);

    ca_reader *r = nullptr;
    CA_CHECK_EQ(CA_OK, ca_media_source_open(src, &r, &err));
    CA_CHECK(r != nullptr);
    CA_CHECK_EQ(9, ca_reader_size(r));

    uint8_t buf[9];
    CA_CHECK_EQ(9, ca_reader_read(r, buf, sizeof(buf)));
    CA_CHECK(memcmp(buf, "test data", 9) == 0);

    ca_reader_destroy(r);
    ca_media_source_destroy(src);
    ca_fs_remove(TEST_FILE, &err);
}

static void test_path_source_errors(void) {
    ca_error err;
    ca_error_init(&err);

    ca_media_source *src = nullptr;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_media_source_path(&src, "", &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_media_source_path(nullptr, "x", &err));
    CA_CHECK(src == nullptr);
}

static void test_descriptor_source(void) {
    ca_error err;
    ca_error_init(&err);
    create_test_file(TEST_FILE, "descriptor test", 15);

#if defined(_WIN32)
    int fd = (int)_open(TEST_FILE, _O_RDONLY | _O_BINARY);
#else
    int fd = open(TEST_FILE, O_RDONLY);
#endif
    CA_CHECK(fd >= 0);

    ca_media_source *src = nullptr;
    CA_CHECK_EQ(CA_OK, ca_media_source_descriptor(&src, fd, true, true, true,
                                                   "my descriptor", &err));
    CA_CHECK(src != nullptr);

    CA_CHECK_EQ(CA_MEDIA_SOURCE_DESCRIPTOR, ca_media_source_kind(src));
    CA_CHECK(strcmp(ca_media_source_display_name(src), "my descriptor") == 0);
    CA_CHECK(ca_media_source_seekable(src));

    ca_reader *r = nullptr;
    CA_CHECK_EQ(CA_OK, ca_media_source_open(src, &r, &err));
    CA_CHECK(r != nullptr);

    /* Descriptor source is single-use */
    CA_CHECK_EQ(CA_NOT_SUPPORTED, ca_media_source_open(src, &r, &err));
    CA_CHECK(ca_media_source_consumed(src));

    uint8_t buf[15];
    CA_CHECK_EQ(15, ca_reader_read(r, buf, sizeof(buf)));
    CA_CHECK(memcmp(buf, "descriptor test", 15) == 0);

    ca_reader_destroy(r);
    ca_media_source_destroy(src);
    ca_fs_remove(TEST_FILE, &err);
}

static void test_descriptor_source_errors(void) {
    ca_error err;
    ca_error_init(&err);

    ca_media_source *src = nullptr;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_media_source_descriptor(&src, -1, true, true, true, "x", &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_media_source_descriptor(nullptr, 1, true, true, true, "x", &err));
}

int main(void) {
    test_path_source();
    test_path_source_errors();
    test_descriptor_source();
    test_descriptor_source_errors();
    return ca_test_finish("source");
}
