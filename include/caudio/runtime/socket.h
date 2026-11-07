#ifndef CAUDIO_RUNTIME_SOCKET_H
#define CAUDIO_RUNTIME_SOCKET_H

/**
 * @file caudio/runtime/socket.h
 * @brief TCP/UDP socket operations.
 *
 * POSIX backend: BSD sockets. Win32 backend: Winsock2.
 * All functions return ca_status and take an optional ca_error *err.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #if !defined(__CYGWIN__)
        #include <afunix.h>
    #endif
#else
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Socket address family.
 */
typedef enum ca_socket_domain {
    CA_SOCKET_DOMAIN_UNIX = 0,
    CA_SOCKET_DOMAIN_INET = 1,
    CA_SOCKET_DOMAIN_INET6 = 2,
} ca_socket_domain;

/**
 * @brief Socket type.
 */
typedef enum ca_socket_type {
    CA_SOCKET_TYPE_STREAM = 0,
    CA_SOCKET_TYPE_DGRAM = 1,
} ca_socket_type;

/**
 * @brief Opaque socket handle.
 */
typedef struct ca_socket *ca_socket_t;

/**
 * @brief Socket address.
 */
typedef struct ca_socket_addr {
    ca_socket_domain domain;
    union {
        struct sockaddr_storage storage;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } addr;
    socklen_t len;
} ca_socket_addr_t;

/**
 * @brief Create a socket.
 *
 * @param domain  Address family.
 * @param type    Socket type (stream or datagram).
 * @param err     Optional error record.
 * @return New socket handle, or CA_SOCKET_INVALID on failure.
 */
CAUDIO_API ca_socket_t ca_socket_create(ca_socket_domain domain,
                                        ca_socket_type type,
                                        ca_error *err);

#define CA_SOCKET_INVALID ((ca_socket_t)nullptr)

/**
 * @brief Fill @p out with the address of a UNIX-domain socket at @p path.
 *
 * @p path is UTF-8 on every platform. The length limit is the platform's
 * `sun_path` capacity (108 bytes on Linux and Windows, 104 on macOS), including
 * the terminating NUL.
 *
 * @return CA_OK on success, CA_INVALID_ARGUMENT for a NULL or empty path, or
 *         CA_OVERFLOW if the path does not fit in `sun_path`.
 */
CAUDIO_API ca_status ca_socket_addr_un(ca_socket_addr_t *out, const char *path,
                                       ca_error *err);

/**
 * @brief Bind @p sock to @p addr.
 *
 * @return CA_OK on success, CA_ALREADY_EXISTS if the address is in use, or
 *         another ca_status.
 */
CAUDIO_API ca_status ca_socket_bind(ca_socket_t sock,
                                    const ca_socket_addr_t *addr,
                                    ca_error *err);

/**
 * @brief Listen for incoming connections on @p sock.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_socket_listen(ca_socket_t sock, int backlog,
                                      ca_error *err);

/**
 * @brief Connect @p sock to @p addr.
 *
 * A blocking socket is connected before returning. Transient failures that a
 * caller may retry (no listener yet, would block, already in progress) are
 * reported as CA_BUSY.
 *
 * @return CA_OK on success, CA_BUSY if the connection is not ready yet, or
 *         another ca_status.
 */
CAUDIO_API ca_status ca_socket_connect(ca_socket_t sock,
                                       const ca_socket_addr_t *addr,
                                       ca_error *err);

/**
 * @brief Accept an incoming connection.
 *
 * @param out_addr Receives the peer address, or NULL if not wanted.
 * @return New connected socket handle, or CA_SOCKET_INVALID on failure.
 */
CAUDIO_API ca_socket_t ca_socket_accept(ca_socket_t sock,
                                        ca_socket_addr_t *out_addr,
                                        ca_error *err);

/**
 * @brief Send @p len bytes from @p buf on @p sock.
 *
 * @return Bytes sent, or (size_t)-1 on error with @p err set.
 */
CAUDIO_API size_t ca_socket_send(ca_socket_t sock, const void *buf,
                                 size_t len, int flags, ca_error *err);

/**
 * @brief Receive up to @p len bytes into @p buf from @p sock.
 *
 * @return Bytes received (0 on EOF/connection closed), or (size_t)-1 on
 *         error with @p err set.
 */
CAUDIO_API size_t ca_socket_recv(ca_socket_t sock, void *buf, size_t len,
                                 int flags, ca_error *err);

/**
 * @brief Close @p sock. Safe to call on CA_SOCKET_INVALID (no-op).
 */
CAUDIO_API void ca_socket_close(ca_socket_t sock);

/**
 * @brief Set or clear blocking mode on @p sock.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_socket_setblocking(ca_socket_t sock, int blocking,
                                           ca_error *err);

/**
 * @brief Wait until @p sock is readable or @p timeout_ms elapses.
 *
 * @param timeout_ms Negative to wait indefinitely, 0 to poll, positive for a
 *                   bounded wait in milliseconds.
 * @return CA_OK if readable, CA_TIMED_OUT on timeout, or another ca_status.
 */
CAUDIO_API ca_status ca_socket_wait_readable(ca_socket_t sock, int timeout_ms,
                                             ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_SOCKET_H */
