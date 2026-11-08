#include <caudio.h>
#include <caudio/runtime/shm.h>
#include <string.h>

#include "ca_test.h"

static int test_map_unmap(void) {
    ca_error err;
    ca_error_init(&err);

    ca_shm_t shm = ca_shm_open("caudio_test_shm", 4096, 1, &err);
    CA_CHECK(shm != CA_SHM_INVALID);
    CA_CHECK_EQ(CA_OK, err.code);

    void* addr = nullptr;
    CA_CHECK_EQ(CA_OK, ca_shm_map(shm, 0, 0, &addr, &err));
    CA_CHECK(addr != nullptr);
    CA_CHECK_EQ(CA_OK, err.code);

    /* Write and read back. */
    const char* msg = "shared memory test";
    memcpy(addr, msg, strlen(msg) + 1);

    /* Remap to verify data persists. */
    void* addr2 = nullptr;
    CA_CHECK_EQ(CA_OK, ca_shm_map(shm, 0, 0, &addr2, &err));
    CA_CHECK_EQ(0, strcmp((const char*)addr2, msg));

    CA_CHECK_EQ(CA_OK, ca_shm_unmap(addr, 0, &err));
    CA_CHECK_EQ(CA_OK, ca_shm_unmap(addr2, 0, &err));

    CA_CHECK_EQ(CA_OK, ca_shm_close(shm, &err));
    CA_CHECK_EQ(CA_OK, ca_shm_unlink("caudio_test_shm", &err));

    return ca_test_finish("shm.map_unmap");
}

static int test_null_args(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_shm_map(CA_SHM_INVALID, 0, 0, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_shm_unmap(nullptr, 0, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_shm_close(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_shm_unlink(nullptr, &err));

    return ca_test_finish("shm.null_args");
}

int main(void) {
    int failures = 0;
    failures += test_map_unmap();
    failures += test_null_args();
    return failures;
}
