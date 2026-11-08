#include <caudio.h>
#include <caudio/runtime/endpoint.h>
#include <caudio/runtime/fs.h>
#include <caudio/runtime/time.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#endif

#include "ca_test.h"

static long process_id(void) {
#if defined(_WIN32) && !defined(__CYGWIN__)
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}

static void get_cwd(char* out, size_t size) {
#if defined(_WIN32) && !defined(__CYGWIN__)
    if (_getcwd(out, (int)size) == nullptr) {
#else
    if (getcwd(out, size) == nullptr) {
#endif
        out[0] = '.';
        out[1] = '\0';
    }
}

static void make_socket_path(char* out, size_t size, const char* tag) {
    char cwd[512];
    get_cwd(cwd, sizeof(cwd));

#if defined(_WIN32) && !defined(__CYGWIN__)
    (void)snprintf(out, size, "%s\\caudio_ep_%ld_%s.sock", cwd, process_id(), tag);
#else
    (void)snprintf(out, size, "%s/caudio_ep_%ld_%s.sock", cwd, process_id(), tag);
#endif
}

static int test_roundtrip(void) {
    char path[512];
    make_socket_path(path, sizeof(path), "rt");
    (void)ca_fs_remove(path, nullptr);

    ca_error err;
    ca_error_init(&err);

    ca_endpoint* server = nullptr;
    CA_CHECK_EQ(CA_OK, ca_endpoint_listen(&server, path, 4, &err));
    CA_CHECK(server != nullptr);
    CA_CHECK(ca_endpoint_is_server(server));
    CA_CHECK(ca_endpoint_name(server) != nullptr);
    CA_CHECK_EQ(0, strcmp(ca_endpoint_name(server), path));

    ca_endpoint* client = nullptr;
    CA_CHECK_EQ(CA_OK, ca_endpoint_connect(&client, path, 2000, &err));
    CA_CHECK(client != nullptr);
    CA_CHECK(!ca_endpoint_is_server(client));

    ca_endpoint* accepted = nullptr;
    CA_CHECK_EQ(CA_OK, ca_endpoint_accept(server, 2000, &accepted, &err));
    CA_CHECK(accepted != nullptr);
    CA_CHECK(!ca_endpoint_is_server(accepted));
    CA_CHECK(ca_endpoint_name(accepted) == nullptr);

    const char* ping = "ping over af_unix";
    CA_CHECK_EQ((long long)strlen(ping),
                (long long)ca_endpoint_write(client, ping, strlen(ping), &err));
    CA_CHECK_EQ(CA_OK, err.code);

    char buffer[64] = {0};
    size_t read = ca_endpoint_read(accepted, buffer, sizeof(buffer) - 1, &err);
    CA_CHECK_EQ((long long)strlen(ping), (long long)read);
    CA_CHECK_EQ(0, strcmp(buffer, ping));

    const char* pong = "pong";
    CA_CHECK_EQ((long long)strlen(pong),
                (long long)ca_endpoint_write(accepted, pong, strlen(pong), &err));
    memset(buffer, 0, sizeof(buffer));
    read = ca_endpoint_read(client, buffer, sizeof(buffer) - 1, &err);
    CA_CHECK_EQ((long long)strlen(pong), (long long)read);
    CA_CHECK_EQ(0, strcmp(buffer, pong));

    CA_CHECK_EQ(CA_OK, ca_endpoint_flush(client, &err));
    CA_CHECK_EQ(CA_OK, ca_endpoint_flush(accepted, &err));

    /* Zero-length writes are a successful no-op. */
    CA_CHECK_EQ(0, (long long)ca_endpoint_write(client, nullptr, 0, &err));

    ca_endpoint_close(&accepted);
    ca_endpoint_close(&client);

    /* The listening path exists until the server closes. */
    CA_CHECK(ca_fs_exists(path, nullptr));
    ca_endpoint_close(&server);
    CA_CHECK(server == nullptr);
    CA_CHECK(!ca_fs_exists(path, nullptr));

    return ca_test_finish("endpoint.roundtrip");
}

static int test_accept_timeout(void) {
    char path[512];
    make_socket_path(path, sizeof(path), "to");
    (void)ca_fs_remove(path, nullptr);

    ca_error err;
    ca_error_init(&err);

    ca_endpoint* server = nullptr;
    CA_CHECK_EQ(CA_OK, ca_endpoint_listen(&server, path, 1, &err));

    ca_endpoint* accepted = nullptr;
    ca_time_us_t start = 0;
    ca_time_us_t end = 0;
    CA_CHECK_EQ(CA_OK, ca_time_now(&start, nullptr));

    CA_CHECK_EQ(CA_TIMED_OUT, ca_endpoint_accept(server, 50, &accepted, &err));
    CA_CHECK_EQ(CA_TIMED_OUT, err.code);
    CA_CHECK(accepted == nullptr);

    CA_CHECK_EQ(CA_OK, ca_time_now(&end, nullptr));
    CA_CHECK(end - start >= 40000);

    /* timeout 0 polls once and does not block. */
    CA_CHECK_EQ(CA_TIMED_OUT, ca_endpoint_accept(server, 0, &accepted, &err));
    CA_CHECK(accepted == nullptr);

    ca_endpoint_close(&server);
    CA_CHECK(!ca_fs_exists(path, nullptr));

    return ca_test_finish("endpoint.accept_timeout");
}

static int test_connect_missing(void) {
    char path[512];
    make_socket_path(path, sizeof(path), "missing");
    (void)ca_fs_remove(path, nullptr);

    ca_error err;
    ca_error_init(&err);

    ca_endpoint* client = nullptr;
    ca_time_us_t start = 0;
    ca_time_us_t end = 0;

    CA_CHECK_EQ(CA_OK, ca_time_now(&start, nullptr));
    CA_CHECK_EQ(CA_TIMED_OUT, ca_endpoint_connect(&client, path, 100, &err));
    CA_CHECK_EQ(CA_TIMED_OUT, err.code);
    CA_CHECK(client == nullptr);
    CA_CHECK_EQ(CA_OK, ca_time_now(&end, nullptr));
    CA_CHECK(end - start >= 80000);

    /* timeout 0 makes exactly one attempt. */
    CA_CHECK_EQ(CA_TIMED_OUT, ca_endpoint_connect(&client, path, 0, &err));
    CA_CHECK(client == nullptr);

    (void)ca_fs_remove(path, nullptr);
    return ca_test_finish("endpoint.connect_missing");
}

static int test_relisten(void) {
    char path[512];
    make_socket_path(path, sizeof(path), "again");
    (void)ca_fs_remove(path, nullptr);

    ca_error err;
    ca_error_init(&err);

    ca_endpoint* server = nullptr;
    CA_CHECK_EQ(CA_OK, ca_endpoint_listen(&server, path, 1, &err));
    ca_endpoint_close(&server);
    CA_CHECK(!ca_fs_exists(path, nullptr));

    /* Closing a listening endpoint unlinks its path, so it is reusable. */
    CA_CHECK_EQ(CA_OK, ca_endpoint_listen(&server, path, 1, &err));
    ca_endpoint_close(&server);
    CA_CHECK(!ca_fs_exists(path, nullptr));

    return ca_test_finish("endpoint.relisten");
}

static int test_invalid_args(void) {
    ca_error err;
    ca_error_init(&err);

    ca_endpoint* endpoint = nullptr;

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_listen(nullptr, "x", 1, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_listen(&endpoint, nullptr, 1, &err));
    CA_CHECK(endpoint == nullptr);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_listen(&endpoint, "", 1, &err));
    CA_CHECK(endpoint == nullptr);

    char long_path[300];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    CA_CHECK_EQ(CA_OVERFLOW, ca_endpoint_listen(&endpoint, long_path, 1, &err));
    CA_CHECK(endpoint == nullptr);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_connect(nullptr, "x", 0, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_connect(&endpoint, nullptr, 0, &err));
    CA_CHECK(endpoint == nullptr);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_connect(&endpoint, "", 0, &err));
    CA_CHECK(endpoint == nullptr);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_accept(nullptr, 0, &endpoint, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_accept(nullptr, 0, nullptr, &err));

    char path[512];
    make_socket_path(path, sizeof(path), "invalid");
    (void)ca_fs_remove(path, nullptr);

    ca_endpoint* server = nullptr;
    CA_CHECK_EQ(CA_OK, ca_endpoint_listen(&server, path, 1, &err));

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_accept(server, 0, nullptr, &err));

    /* A client handle is not a listening endpoint. */
    ca_endpoint* client = nullptr;
    CA_CHECK_EQ(CA_OK, ca_endpoint_connect(&client, path, 2000, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_accept(client, 0, &endpoint, &err));
    CA_CHECK(endpoint == nullptr);

    ca_endpoint* accepted = nullptr;
    CA_CHECK_EQ(CA_OK, ca_endpoint_accept(server, 2000, &accepted, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_accept(accepted, 0, &endpoint, &err));

    CA_CHECK_EQ((size_t)-1, ca_endpoint_read(nullptr, path, 1, &err));
    CA_CHECK_EQ((size_t)-1, ca_endpoint_write(nullptr, path, 1, &err));
    CA_CHECK_EQ((size_t)-1, ca_endpoint_write(server, nullptr, 1, &err));
    CA_CHECK_EQ((size_t)-1, ca_endpoint_read(server, nullptr, 1, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_endpoint_flush(nullptr, &err));

    CA_CHECK(ca_endpoint_name(nullptr) == nullptr);
    CA_CHECK(!ca_endpoint_is_server(nullptr));

    ca_endpoint_close(nullptr);
    ca_endpoint* none = nullptr;
    ca_endpoint_close(&none);

    ca_endpoint_close(&accepted);
    ca_endpoint_close(&client);
    ca_endpoint_close(&server);
    CA_CHECK(!ca_fs_exists(path, nullptr));

    return ca_test_finish("endpoint.invalid_args");
}

int main(void) {
    int failures = 0;

    failures += test_roundtrip();
    failures += test_accept_timeout();
    failures += test_connect_missing();
    failures += test_relisten();
    failures += test_invalid_args();

    return failures;
}
