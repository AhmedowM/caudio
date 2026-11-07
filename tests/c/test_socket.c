#include <caudio.h>
#include <caudio/runtime/socket.h>

#include <string.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

#include "ca_test.h"

static int test_create_bind_listen(void) {
    ca_error err;
    ca_error_init(&err);

    ca_socket_t srv = ca_socket_create(CA_SOCKET_DOMAIN_INET,
                                       CA_SOCKET_TYPE_STREAM, &err);
    CA_CHECK(srv != CA_SOCKET_INVALID);
    CA_CHECK_EQ(CA_OK, err.code);

    /* Bind to localhost:0 (ephemeral port). We can't easily get the port
       back without sockaddr inspection, so just verify bind succeeds. */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* ephemeral */

    ca_socket_addr_t sa = {0};
    sa.domain = CA_SOCKET_DOMAIN_INET;
    sa.addr.in = addr;
    sa.len = sizeof(addr);

    CA_CHECK_EQ(CA_OK, ca_socket_bind(srv, &sa, &err));
    CA_CHECK_EQ(CA_OK, ca_socket_listen(srv, 4, &err));

    ca_socket_close(srv);

    return ca_test_finish("socket.create_bind_listen");
}

static int test_connect_send_recv(void) {
    ca_error err;
    ca_error_init(&err);

    /* Server socket on a fixed high port. */
    ca_socket_t srv = ca_socket_create(CA_SOCKET_DOMAIN_INET,
                                       CA_SOCKET_TYPE_STREAM, &err);
    CA_CHECK(srv != CA_SOCKET_INVALID);

    struct sockaddr_in saddr = {0};
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = htons(19877);

    ca_socket_addr_t sa = {0};
    sa.domain = CA_SOCKET_DOMAIN_INET;
    sa.addr.in = saddr;
    sa.len = sizeof(saddr);

    CA_CHECK_EQ(CA_OK, ca_socket_bind(srv, &sa, &err));
    CA_CHECK_EQ(CA_OK, ca_socket_listen(srv, 4, &err));

    /* Client connects. */
    ca_socket_t cli = ca_socket_create(CA_SOCKET_DOMAIN_INET,
                                       CA_SOCKET_TYPE_STREAM, &err);
    CA_CHECK(cli != CA_SOCKET_INVALID);

    CA_CHECK_EQ(CA_OK, ca_socket_connect(cli, &sa, &err));

    /* Server accepts. */
    ca_socket_t conn = ca_socket_accept(srv, nullptr, &err);
    CA_CHECK(conn != CA_SOCKET_INVALID);
    CA_CHECK_EQ(CA_OK, err.code);

    /* Client sends, server receives. */
    const char *msg = "hello socket";
    CA_CHECK_EQ(strlen(msg),
                ca_socket_send(cli, msg, strlen(msg), 0, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    char buf[64] = {0};
    size_t n = ca_socket_recv(conn, buf, sizeof(buf) - 1, 0, &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(strlen(msg), n);
    CA_CHECK_EQ(0, strcmp(buf, msg));

    /* Server sends back, client receives. */
    const char *reply = "pong";
    CA_CHECK_EQ(strlen(reply),
                ca_socket_send(conn, reply, strlen(reply), 0, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    memset(buf, 0, sizeof(buf));
    n = ca_socket_recv(cli, buf, sizeof(buf) - 1, 0, &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(strlen(reply), n);
    CA_CHECK_EQ(0, strcmp(buf, reply));

    ca_socket_close(conn);
    ca_socket_close(cli);
    ca_socket_close(srv);

    return ca_test_finish("socket.connect_send_recv");
}

static int test_addr_un(void) {
    ca_error err;
    ca_error_init(&err);

    ca_socket_addr_t sa;
    memset(&sa, 0, sizeof(sa));
    CA_CHECK_EQ(CA_OK, ca_socket_addr_un(&sa, "caudio_test.sock", &err));
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(CA_SOCKET_DOMAIN_UNIX, sa.domain);
    CA_CHECK(sa.len > 0);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_socket_addr_un(nullptr, "caudio_test.sock", &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_socket_addr_un(&sa, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_socket_addr_un(&sa, "", &err));

    char long_path[200];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    CA_CHECK_EQ(CA_OVERFLOW, ca_socket_addr_un(&sa, long_path, &err));

    return ca_test_finish("socket.addr_un");
}

static int test_null_args(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_socket_bind(CA_SOCKET_INVALID, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_socket_listen(CA_SOCKET_INVALID, 4, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_socket_connect(CA_SOCKET_INVALID, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_socket_setblocking(CA_SOCKET_INVALID, 1, &err));
    CA_CHECK_EQ((size_t)-1,
                ca_socket_send(CA_SOCKET_INVALID, "x", 1, 0, &err));
    CA_CHECK_EQ((size_t)-1,
                ca_socket_recv(CA_SOCKET_INVALID, "x", 1, 0, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_socket_wait_readable(CA_SOCKET_INVALID, 0, &err));

    return ca_test_finish("socket.null_args");
}

int main(void) {
    int failures = 0;
    failures += test_create_bind_listen();
    failures += test_connect_send_recv();
    failures += test_addr_un();
    failures += test_null_args();
    return failures;
}
