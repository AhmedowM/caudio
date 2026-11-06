#include <caudio.h>
#include <caudio/runtime/thread.h>

#include <string.h>

#include "ca_test.h"

/* Shared counter incremented by all threads under a mutex. */
static int g_counter = 0;
static ca_mutex_t g_mutex = nullptr;
static ca_cond_t g_cond = nullptr;
static int g_cond_flag = 0;

static void *thread_func(void *arg) {
    int id = *(int *)arg;

    for (int i = 0; i < 10000; ++i) {
        ca_mutex_lock(g_mutex, nullptr);
        g_counter++;
        ca_mutex_unlock(g_mutex, nullptr);
    }

    ca_mutex_lock(g_mutex, nullptr);
    g_cond_flag = 1;
    ca_cond_signal(g_cond, nullptr);
    ca_mutex_unlock(g_mutex, nullptr);

    return (void *)(intptr_t)id;
}

static int test_create_join(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_OK, ca_mutex_create(&g_mutex, &err));
    CA_CHECK_EQ(CA_OK, ca_cond_create(&g_cond, &err));

    int ids[4] = {0, 1, 2, 3};
    ca_thread_t threads[4];

    for (int i = 0; i < 4; ++i) {
        CA_CHECK_EQ(CA_OK, ca_thread_create(&threads[i], thread_func,
                                            &ids[i], &err));
    }

    /* Wait for condition signal from a thread. */
    ca_mutex_lock(g_mutex, nullptr);
    while (g_cond_flag == 0) {
        CA_CHECK_EQ(CA_OK, ca_cond_wait(g_cond, g_mutex, &err));
    }
    ca_mutex_unlock(g_mutex, nullptr);

    for (int i = 0; i < 4; ++i) {
        void *ret = nullptr;
        CA_CHECK_EQ(CA_OK, ca_thread_join(threads[i], &ret, &err));
        CA_CHECK_EQ(i, (int)(intptr_t)ret);
    }

    /* Counter should be exactly 4 * 10000 = 40000 with no races. */
    CA_CHECK_EQ(40000, g_counter);

    ca_mutex_destroy(g_mutex);
    ca_cond_destroy(g_cond);

    return ca_test_finish("thread.create_join");
}

static int test_yield_self(void) {
    ca_thread_yield();
    CA_CHECK(ca_thread_self() > 0);

    return ca_test_finish("thread.yield_self");
}

static int test_null_args(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_thread_create(nullptr, nullptr, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_thread_join(nullptr, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_thread_detach(nullptr, &err));

    return ca_test_finish("thread.null_args");
}

int main(void) {
    int failures = 0;
    failures += test_create_join();
    failures += test_yield_self();
    failures += test_null_args();
    return failures;
}
