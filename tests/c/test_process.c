#include <caudio.h>
#include <caudio/runtime/process.h>
#include <caudio/runtime/time.h>

#include <string.h>

#include "ca_test.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#define PROG_ECHO "cmd"
#define ECHO_ARGS "/c", "echo", "hello"
#define PROG_SLEEP "cmd"
#define SLEEP_ARGS "/c", "ping", "-n", "2", "127.0.0.1", ">nul"
#else
#define PROG_ECHO "/bin/echo"
#define ECHO_ARGS "hello"
#define PROG_SLEEP "/bin/sleep"
#define SLEEP_ARGS "100"
#endif

static int test_spawn_wait(void) {
    ca_error err;
    ca_error_init(&err);

    ca_process_t p = nullptr;
#if defined(_WIN32) || defined(__CYGWIN__)
    char *argv[] = {(char *)PROG_ECHO, (char *)ECHO_ARGS, nullptr};
#else
    char *argv[] = {(char *)PROG_ECHO, (char *)ECHO_ARGS, nullptr};
#endif

    CA_CHECK_EQ(CA_OK, ca_process_spawn(&p, argv, nullptr, &err));
    CA_CHECK(p != nullptr);
    CA_CHECK_EQ(CA_OK, err.code);

    ca_process_status status = {0, 0};
    CA_CHECK_EQ(CA_OK, ca_process_wait(p, &status, &err));
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK(status.exited);
    CA_CHECK_EQ(0, status.code);

    return ca_test_finish("process.spawn_wait");
}

static int test_self(void) {
    int pid = ca_process_self();
    CA_CHECK(pid > 0);

    return ca_test_finish("process.self");
}

static int test_kill(void) {
    ca_error err;
    ca_error_init(&err);

    /* Spawn a long-running process, then kill it. */
#if defined(_WIN32) || defined(__CYGWIN__)
    char *argv[] = {(char *)PROG_SLEEP, (char *)SLEEP_ARGS, nullptr};
#else
    char *argv[] = {(char *)PROG_SLEEP, (char *)SLEEP_ARGS, nullptr};
#endif

    ca_process_t p = nullptr;
    CA_CHECK_EQ(CA_OK, ca_process_spawn(&p, argv, nullptr, &err));
    CA_CHECK(p != nullptr);

    /* Give it a moment to start. */
    ca_time_sleep_us(50000, &err);

    CA_CHECK_EQ(CA_OK, ca_process_kill(p, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    ca_process_status status = {0, 0};
    CA_CHECK_EQ(CA_OK, ca_process_wait(p, &status, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    /* Killed process: may be signaled with nonzero code. */
    CA_CHECK(!status.exited || status.code != 0);

    return ca_test_finish("process.kill");
}

static int test_release(void) {
    ca_error err;
    ca_error_init(&err);

    char *argv[] = {(char *)PROG_ECHO, (char *)ECHO_ARGS, nullptr};

    ca_process_t p = nullptr;
    CA_CHECK_EQ(CA_OK, ca_process_spawn(&p, argv, nullptr, &err));
    CA_CHECK(p != nullptr);

    CA_CHECK_EQ(CA_OK, ca_process_release(p, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    return ca_test_finish("process.release");
}

int main(void) {
    int failures = 0;
    failures += test_spawn_wait();
    failures += test_self();
    failures += test_kill();
    failures += test_release();
    return failures;
}
