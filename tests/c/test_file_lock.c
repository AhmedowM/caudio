#include <caudio.h>
#include <caudio/runtime/file_lock.h>
#include <stdio.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#endif

#include "ca_test.h"

static const char* LOCK_FILE = "caudio_test_file_lock.dat";

static int test_lock_unlock(void) {
    ca_error err;
    ca_error_init(&err);

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Create a test file. */
    HANDLE fh = CreateFileW(L"caudio_test_file_lock.dat", GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    CA_CHECK(fh != INVALID_HANDLE_VALUE);

    /* Lock the whole file exclusively. */
    CA_CHECK_EQ(CA_OK, ca_file_lock(fh, CA_FILE_LOCK_WRITE, 0, 0, 1, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    /* Unlock. */
    CA_CHECK_EQ(CA_OK, ca_file_unlock(fh, 0, 0, &err));
    CloseHandle(fh);

    /* Clean up. */
    DeleteFileW(L"caudio_test_file_lock.dat");
#else
    /* Create a test file. */
    FILE* fp = fopen(LOCK_FILE, "w+");
    CA_CHECK(fp != nullptr);
    fclose(fp);

    /* Lock the whole file exclusively. */
    fp = fopen(LOCK_FILE, "r+");
    CA_CHECK(fp != nullptr);
    CA_CHECK_EQ(CA_OK,
                ca_file_lock((void*)(intptr_t)fileno(fp), CA_FILE_LOCK_WRITE, 0, 0, 1, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    /* Unlock. */
    CA_CHECK_EQ(CA_OK, ca_file_unlock((void*)(intptr_t)fileno(fp), 0, 0, &err));
    fclose(fp);

    /* Clean up. */
    remove(LOCK_FILE);
#endif

    return ca_test_finish("file_lock.lock_unlock");
}

static int test_null_args(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_file_lock(nullptr, CA_FILE_LOCK_WRITE, 0, 0, 1, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_file_unlock(nullptr, 0, 0, &err));

    return ca_test_finish("file_lock.null_args");
}

int main(void) {
    int failures = 0;
    failures += test_lock_unlock();
    failures += test_null_args();
    return failures;
}
