#include <caudio.h>
#include <caudio/runtime/fs.h>
#include <string.h>

#include "ca_test.h"

static const char* TEST_DIR = "caudio_test_fs_dir";

static int test_write_read(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs(TEST_DIR, &err));

    ca_file_t f = ca_fs_open("caudio_test_fs_dir/hello.txt",
                             CA_FS_WRITE | CA_FS_CREATE | CA_FS_TRUNCATE, &err);
    CA_CHECK(f != CA_FS_INVALID);
    CA_CHECK_EQ(CA_OK, err.code);

    const char* msg = "hello ca_runtime fs";
    size_t written = ca_fs_write(f, msg, strlen(msg), &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(strlen(msg), written);
    ca_fs_close(f);

    f = ca_fs_open("caudio_test_fs_dir/hello.txt", CA_FS_READ, &err);
    CA_CHECK(f != CA_FS_INVALID);
    CA_CHECK_EQ(CA_OK, err.code);

    char buf[64] = {0};
    size_t read = ca_fs_read(f, buf, sizeof(buf) - 1, &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(strlen(msg), read);
    CA_CHECK_EQ(0, strcmp(buf, msg));

    /* size must be queried while the handle is still open */
    uint64_t sz = ca_fs_size(f, &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(strlen(msg), sz);

    ca_fs_close(f);

    return ca_test_finish("fs.write_read");
}

static int test_exists(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK(ca_fs_exists("caudio_test_fs_dir/hello.txt", &err));
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK(!ca_fs_exists("caudio_test_fs_dir/nope.txt", &err));
    CA_CHECK_EQ(CA_OK, err.code);

    return ca_test_finish("fs.exists");
}

static int test_remove(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_OK, ca_fs_remove("caudio_test_fs_dir/hello.txt", &err));
    CA_CHECK(!ca_fs_exists("caudio_test_fs_dir/hello.txt", &err));

    /* remove empty dir */
    CA_CHECK_EQ(CA_OK, ca_fs_remove("caudio_test_fs_dir", &err));
    CA_CHECK(!ca_fs_exists("caudio_test_fs_dir", &err));

    /* remove nonexistent path returns error */
    CA_CHECK_EQ(CA_IO_ERROR, ca_fs_remove("caudio_test_fs_dir/nope.txt", &err));

    return ca_test_finish("fs.remove");
}

static int test_mkdirs_nested(void) {
    ca_error err;
    ca_error_init(&err);

    /* nested dirs create parents */
    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs("caudio_test_fs_dir/a/b/c", &err));
    CA_CHECK(ca_fs_exists("caudio_test_fs_dir/a/b/c", &err));
    CA_CHECK_EQ(CA_OK, err.code);

    /* cleanup */
    ca_fs_remove("caudio_test_fs_dir/a/b/c", &err);
    ca_fs_remove("caudio_test_fs_dir/a/b", &err);
    ca_fs_remove("caudio_test_fs_dir/a", &err);

    return ca_test_finish("fs.mkdirs_nested");
}

int main(void) {
    int failures = 0;
    ca_error err;
    ca_error_init(&err);
    /* Clean up any leftover state from previous runs. */
    ca_fs_remove("caudio_test_fs_dir", &err);

    failures += test_write_read();
    failures += test_exists();
    failures += test_remove();
    failures += test_mkdirs_nested();
    return failures;
}
