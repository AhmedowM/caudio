#include <caudio/media/reader.h>
#include <caudio/runtime/fs.h>
#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>

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

static const char *TEST_FILE = "caudio_test_reader_data.bin";

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

static void test_reader_file(void) {
    const uint8_t data[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    create_test_file(TEST_FILE, data, sizeof(data));

    ca_error err;
    ca_error_init(&err);
    ca_reader *r = nullptr;
    CA_CHECK_EQ(CA_OK, ca_reader_file(&r, TEST_FILE, &err));
    CA_CHECK(r != nullptr);

    /* Size */
    CA_CHECK_EQ(16, ca_reader_size(r));

    /* Tell at start */
    CA_CHECK_EQ(0, ca_reader_tell(r));

    /* Read all */
    uint8_t buf[16];
    CA_CHECK_EQ(16, ca_reader_read(r, buf, sizeof(buf)));
    CA_CHECK(memcmp(buf, data, sizeof(data)) == 0);
    CA_CHECK_EQ(16, ca_reader_tell(r));

    /* Read past EOF */
    CA_CHECK_EQ(0, ca_reader_read(r, buf, sizeof(buf)));

    /* Seek to middle and read */
    CA_CHECK_EQ(CA_OK, ca_reader_seek(r, 4, SEEK_SET));
    CA_CHECK_EQ(4, ca_reader_tell(r));
    CA_CHECK_EQ(12, ca_reader_read(r, buf, sizeof(buf)));
    CA_CHECK(memcmp(buf, data + 4, 12) == 0);

    /* Seek relative */
    CA_CHECK_EQ(CA_OK, ca_reader_seek(r, -4, SEEK_CUR));
    CA_CHECK_EQ(12, ca_reader_tell(r));

    /* Seek from end */
    CA_CHECK_EQ(CA_OK, ca_reader_seek(r, -4, SEEK_END));
    CA_CHECK_EQ(12, ca_reader_tell(r));

    /* Seekable */
    CA_CHECK(ca_reader_seekable(r));

    ca_reader_destroy(r);
    ca_fs_remove(TEST_FILE, &err);
}

static void test_reader_file_errors(void) {
    ca_error err;
    ca_error_init(&err);
    ca_reader *r = nullptr;

    /* NULL args */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_reader_file(nullptr, "x", &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_reader_file(&r, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_reader_file(&r, "", &err));

    /* Nonexistent file */
    CA_CHECK_EQ(CA_NOT_FOUND, ca_reader_file(&r, "caudio_nonexistent_file_xyz.bin", &err));
    CA_CHECK(r == nullptr);
}

static void test_reader_memory(void) {
    const uint8_t data[] = {0xde, 0xad, 0xbe, 0xef};
    ca_error err;
    ca_error_init(&err);

    ca_reader *r = nullptr;
    CA_CHECK_EQ(CA_OK, ca_reader_memory(&r, data, sizeof(data), &err));
    CA_CHECK(r != nullptr);
    CA_CHECK_EQ(4, ca_reader_size(r));
    CA_CHECK(ca_reader_seekable(r));

    uint8_t buf[4];
    CA_CHECK_EQ(4, ca_reader_read(r, buf, sizeof(buf)));
    CA_CHECK(memcmp(buf, data, sizeof(data)) == 0);

    /* Read past EOF */
    CA_CHECK_EQ(0, ca_reader_read(r, buf, sizeof(buf)));

    /* Seek */
    CA_CHECK_EQ(CA_OK, ca_reader_seek(r, 2, SEEK_SET));
    CA_CHECK_EQ(2, ca_reader_tell(r));
    CA_CHECK_EQ(2, ca_reader_read(r, buf, sizeof(buf)));
    CA_CHECK(buf[0] == 0xbe);
    CA_CHECK(buf[1] == 0xef);

    ca_reader_destroy(r);

    /* NULL data with len 0 */
    CA_CHECK_EQ(CA_OK, ca_reader_memory(&r, nullptr, 0, &err));
    CA_CHECK(r != nullptr);
    CA_CHECK_EQ(0, ca_reader_size(r));
    CA_CHECK_EQ(0, ca_reader_read(r, buf, sizeof(buf)));
    ca_reader_destroy(r);

    /* NULL data with len > 0 */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_reader_memory(&r, nullptr, 4, &err));
}

static void test_reader_descriptor(void) {
    ca_error err;
    ca_error_init(&err);

    /* Create a temp file, open its fd */
    create_test_file(TEST_FILE, (const uint8_t *)"hello world", 11);

#if defined(_WIN32)
    int fd = (int)_open(TEST_FILE, _O_RDONLY | _O_BINARY);
#else
    int fd = open(TEST_FILE, O_RDONLY);
#endif
    CA_CHECK(fd >= 0);

    ca_reader *r = nullptr;
    CA_CHECK_EQ(CA_OK, ca_reader_descriptor(&r, fd, true, true, true, &err));
    CA_CHECK(r != nullptr);
    CA_CHECK_EQ(11, ca_reader_size(r));
    CA_CHECK(ca_reader_seekable(r));

    uint8_t buf[11];
    CA_CHECK_EQ(11, ca_reader_read(r, buf, sizeof(buf)));
    CA_CHECK(memcmp(buf, "hello world", 11) == 0);

    ca_reader_destroy(r);
    ca_fs_remove(TEST_FILE, &err);

    /* Invalid fd */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_reader_descriptor(&r, -1, true, true, true, &err));
}

int main(void) {
    test_reader_file();
    test_reader_file_errors();
    test_reader_memory();
    test_reader_descriptor();
    return ca_test_finish("reader");
}
