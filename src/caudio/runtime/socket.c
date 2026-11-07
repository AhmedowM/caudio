#include <caudio/runtime/socket.h>

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <poll.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

struct ca_socket {
#if defined(_WIN32) || defined(__CYGWIN__)
    SOCKET handle;
#else
    int fd;
#endif
};

#if defined(_WIN32) || defined(__CYGWIN__)
static int socket_init_winsock(void) {
    static int initialized = 0;
    if (!initialized) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return -1;
        }
        initialized = 1;
    }
    return 0;
}

static ca_status socket_bind_status(int code) {
    if (code == WSAEADDRINUSE) {
        return CA_ALREADY_EXISTS;
    }
    if (code == WSAEACCES) {
        return CA_PERMISSION_DENIED;
    }
    return CA_IO_ERROR;
}

static ca_status socket_connect_status(int code) {
    if (code == WSAECONNREFUSED || code == WSAEWOULDBLOCK ||
        code == WSAEINPROGRESS || code == WSAEINTR) {
        return CA_BUSY;
    }
    if (code == WSAEACCES) {
        return CA_PERMISSION_DENIED;
    }
    if (code == WSAETIMEDOUT) {
        return CA_TIMED_OUT;
    }
    return CA_IO_ERROR;
}
#else
static ca_status socket_bind_status(int code) {
    if (code == EADDRINUSE || code == EEXIST) {
        return CA_ALREADY_EXISTS;
    }
    if (code == EACCES) {
        return CA_PERMISSION_DENIED;
    }
    return CA_IO_ERROR;
}

static ca_status socket_connect_status(int code) {
    if (code == ECONNREFUSED || code == ENOENT || code == EAGAIN ||
        code == EINPROGRESS || code == EINTR
#if EWOULDBLOCK != EAGAIN
        || code == EWOULDBLOCK
#endif
    ) {
        return CA_BUSY;
    }
    if (code == EACCES) {
        return CA_PERMISSION_DENIED;
    }
    if (code == ETIMEDOUT) {
        return CA_TIMED_OUT;
    }
    return CA_IO_ERROR;
}
#endif

ca_status ca_socket_addr_un(ca_socket_addr_t *out, const char *path,
                            ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr || path == nullptr || path[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "out or path is NULL or empty");
        }
        return CA_INVALID_ARGUMENT;
    }

    size_t path_len = strlen(path);

    struct sockaddr_un un;
    memset(&un, 0, sizeof(un));

    if (path_len >= sizeof(un.sun_path)) {
        if (err) {
            ca_error_set(err, CA_OVERFLOW,
                         "UNIX socket path needs %zu bytes; limit is %zu",
                         path_len + 1, sizeof(un.sun_path));
        }
        return CA_OVERFLOW;
    }

    un.sun_family = AF_UNIX;
    memcpy(un.sun_path, path, path_len + 1);

    memset(out, 0, sizeof(*out));
    out->domain = CA_SOCKET_DOMAIN_UNIX;
    memcpy(&out->addr.storage, &un,
           offsetof(struct sockaddr_un, sun_path) + path_len + 1);
    out->len =
        (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1);

    return CA_OK;
}

ca_socket_t ca_socket_create(ca_socket_domain domain,
                             ca_socket_type type, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (socket_init_winsock() != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "WSAStartup failed");
        }
        return CA_SOCKET_INVALID;
    }
#endif

    int af = 0, st = 0, proto = 0;

    switch (domain) {
        case CA_SOCKET_DOMAIN_INET:
            af = AF_INET;
            break;
        case CA_SOCKET_DOMAIN_INET6:
            af = AF_INET6;
            break;
        case CA_SOCKET_DOMAIN_UNIX:
            af = AF_UNIX;
            break;
        default:
            if (err) {
                ca_error_set(err, CA_INVALID_ARGUMENT, "invalid domain");
            }
            return CA_SOCKET_INVALID;
    }

    switch (type) {
        case CA_SOCKET_TYPE_STREAM:
            st = SOCK_STREAM;
            proto = 0;
            break;
        case CA_SOCKET_TYPE_DGRAM:
            st = SOCK_DGRAM;
            proto = 0;
            break;
        default:
            if (err) {
                ca_error_set(err, CA_INVALID_ARGUMENT, "invalid type");
            }
            return CA_SOCKET_INVALID;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    SOCKET handle = socket(af, st, proto);
    if (handle == INVALID_SOCKET) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "socket failed");
        }
        return CA_SOCKET_INVALID;
    }
#else
    int fd = socket(af, st, proto);
    if (fd < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "socket failed: %d", errno);
        }
        return CA_SOCKET_INVALID;
    }
#endif

    ca_socket_t sock = (ca_socket_t)calloc(1, sizeof(struct ca_socket));
    if (sock == nullptr) {
#if defined(_WIN32) || defined(__CYGWIN__)
        closesocket(handle);
#else
        close(fd);
#endif
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate socket");
        }
        return CA_SOCKET_INVALID;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    sock->handle = handle;
#else
    sock->fd = fd;
#endif

    return sock;
}

ca_status ca_socket_bind(ca_socket_t sock,
                         const ca_socket_addr_t *addr, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (sock == CA_SOCKET_INVALID || addr == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "invalid socket or addr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (bind(sock->handle, (const struct sockaddr *)&addr->addr.storage,
             addr->len) == SOCKET_ERROR) {
        int code = WSAGetLastError();
        if (err) {
            ca_error_set(err, socket_bind_status(code), "bind failed: %d",
                         code);
        }
        return socket_bind_status(code);
    }
#else
    if (bind(sock->fd, (const struct sockaddr *)&addr->addr.storage,
             addr->len) != 0) {
        int code = errno;
        if (err) {
            ca_error_set(err, socket_bind_status(code), "bind failed: %d",
                         code);
        }
        return socket_bind_status(code);
    }
#endif

    return CA_OK;
}

ca_status ca_socket_listen(ca_socket_t sock, int backlog, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (sock == CA_SOCKET_INVALID) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid socket");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (listen(sock->handle, backlog) == SOCKET_ERROR) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "listen failed");
        }
        return CA_IO_ERROR;
    }
#else
    if (listen(sock->fd, backlog) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "listen failed: %d", errno);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_socket_connect(ca_socket_t sock,
                            const ca_socket_addr_t *addr, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (sock == CA_SOCKET_INVALID || addr == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "invalid socket or addr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (connect(sock->handle, (const struct sockaddr *)&addr->addr.storage,
                addr->len) == SOCKET_ERROR) {
        int code = WSAGetLastError();
        if (err) {
            ca_error_set(err, socket_connect_status(code),
                         "connect failed: %d", code);
        }
        return socket_connect_status(code);
    }
#else
    if (connect(sock->fd, (const struct sockaddr *)&addr->addr.storage,
                addr->len) != 0) {
        int code = errno;
        if (err) {
            ca_error_set(err, socket_connect_status(code),
                         "connect failed: %d", code);
        }
        return socket_connect_status(code);
    }
#endif

    return CA_OK;
}

ca_socket_t ca_socket_accept(ca_socket_t sock,
                             ca_socket_addr_t *out_addr, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (sock == CA_SOCKET_INVALID) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid socket");
        }
        return CA_SOCKET_INVALID;
    }

    (void)out_addr;

#if defined(_WIN32) || defined(__CYGWIN__)
    SOCKET handle = accept(sock->handle, nullptr, nullptr);
    if (handle == INVALID_SOCKET) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "accept failed");
        }
        return CA_SOCKET_INVALID;
    }
#else
    int fd = accept(sock->fd, nullptr, nullptr);
    if (fd < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "accept failed: %d", errno);
        }
        return CA_SOCKET_INVALID;
    }
#endif

    ca_socket_t new_sock = (ca_socket_t)calloc(1, sizeof(struct ca_socket));
    if (new_sock == nullptr) {
#if defined(_WIN32) || defined(__CYGWIN__)
        closesocket(handle);
#else
        close(fd);
#endif
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate socket");
        }
        return CA_SOCKET_INVALID;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    new_sock->handle = handle;
#else
    new_sock->fd = fd;
#endif

    return new_sock;
}

size_t ca_socket_send(ca_socket_t sock, const void *buf, size_t len,
                      int flags, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (sock == CA_SOCKET_INVALID || buf == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "invalid socket or buf");
        }
        return (size_t)-1;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    int sent = send(sock->handle, (const char *)buf, (int)len, flags);
    if (sent == SOCKET_ERROR) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "send failed");
        }
        return (size_t)-1;
    }
    return (size_t)sent;
#else
    ssize_t n = send(sock->fd, buf, len, flags);
    if (n < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "send failed: %d", errno);
        }
        return (size_t)-1;
    }
    return (size_t)n;
#endif
}

size_t ca_socket_recv(ca_socket_t sock, void *buf, size_t len, int flags,
                      ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (sock == CA_SOCKET_INVALID || buf == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "invalid socket or buf");
        }
        return (size_t)-1;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    int received = recv(sock->handle, (char *)buf, (int)len, flags);
    if (received == SOCKET_ERROR) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "recv failed");
        }
        return (size_t)-1;
    }
    return (size_t)received;
#else
    ssize_t n = recv(sock->fd, buf, len, flags);
    if (n < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "recv failed: %d", errno);
        }
        return (size_t)-1;
    }
    return (size_t)n;
#endif
}

void ca_socket_close(ca_socket_t sock) {
    if (sock == CA_SOCKET_INVALID) {
        return;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    closesocket(sock->handle);
#else
    close(sock->fd);
#endif

    free(sock);
}

ca_status ca_socket_setblocking(ca_socket_t sock, int blocking,
                                ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (sock == CA_SOCKET_INVALID) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid socket");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    u_long mode = blocking ? 0 : 1;
    if (ioctlsocket(sock->handle, FIONBIO, &mode) != NO_ERROR) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "ioctlsocket failed");
        }
        return CA_IO_ERROR;
    }
#else
    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "fcntl F_GETFL failed: %d", errno);
        }
        return CA_IO_ERROR;
    }

    if (blocking) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }

    if (fcntl(sock->fd, F_SETFL, flags) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "fcntl F_SETFL failed: %d", errno);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_socket_wait_readable(ca_socket_t sock, int timeout_ms,
                                  ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (sock == CA_SOCKET_INVALID) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid socket");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock->handle, &readfds);

    struct timeval tv;
    struct timeval *ptv = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }

    int rc = select(0, &readfds, nullptr, nullptr, ptv);
    if (rc == SOCKET_ERROR) {
        int code = WSAGetLastError();
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "select failed: %d", code);
        }
        return CA_IO_ERROR;
    }

    if (rc == 0) {
        if (err) {
            ca_error_set(err, CA_TIMED_OUT, "socket wait timed out");
        }
        return CA_TIMED_OUT;
    }

    return CA_OK;
#else
    struct pollfd pfd;
    pfd.fd = sock->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "poll failed: %d", errno);
        }
        return CA_IO_ERROR;
    }

    if (rc == 0) {
        if (err) {
            ca_error_set(err, CA_TIMED_OUT, "socket wait timed out");
        }
        return CA_TIMED_OUT;
    }

    return CA_OK;
#endif
}
