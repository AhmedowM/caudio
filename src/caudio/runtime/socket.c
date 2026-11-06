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
#endif

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
#if defined(_WIN32) || defined(__CYGWIN__)
            if (err) {
                ca_error_set(err, CA_NOT_SUPPORTED,
                             "UNIX domain sockets not supported on Win32");
            }
            return CA_SOCKET_INVALID;
#else
            af = AF_UNIX;
            break;
#endif
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
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "bind failed");
        }
        return CA_IO_ERROR;
    }
#else
    if (bind(sock->fd, (const struct sockaddr *)&addr->addr.storage,
             addr->len) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "bind failed: %d", errno);
        }
        return CA_IO_ERROR;
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
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "connect failed");
        }
        return CA_IO_ERROR;
    }
#else
    if (connect(sock->fd, (const struct sockaddr *)&addr->addr.storage,
                addr->len) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "connect failed: %d", errno);
        }
        return CA_IO_ERROR;
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
