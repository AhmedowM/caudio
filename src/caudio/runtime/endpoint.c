#include <caudio/runtime/endpoint.h>
#include <caudio/runtime/fs.h>
#include <caudio/runtime/socket.h>
#include <caudio/runtime/time.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <cstdx/allocator.h>

#if !defined(_WIN32) && !defined(__CYGWIN__)
#include <sys/stat.h>
#endif

/**
 * @file caudio/runtime/endpoint.c
 * @brief Local IPC endpoint built on the UNIX-domain socket layer.
 */

struct ca_endpoint {
    ca_socket_t socket;
    char* name;
    bool server;
};

static ca_status endpoint_error_code(const ca_error* err, ca_status fallback) {
    if (err != nullptr && err->code != CA_OK) {
        return err->code;
    }
    return fallback;
}

static ca_status endpoint_wrap(ca_endpoint** out, ca_socket_t socket, const char* name, bool server,
                               ca_error* err) {
    const cx_allocator* alloc = cx_allocator_default();
    ca_endpoint* endpoint = (ca_endpoint*)cx_allocator_alloc(alloc, sizeof(*endpoint), alignof(*endpoint));
    if (endpoint == nullptr) {
        ca_socket_close(socket);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate endpoint");
        }
        return CA_OUT_OF_MEMORY;
    }

    endpoint->name = nullptr;
    endpoint->socket = socket;
    endpoint->server = server;

    if (name != nullptr) {
        size_t len = strlen(name);
        endpoint->name = (char*)cx_allocator_alloc(alloc, len + 1, 1);
        if (endpoint->name == nullptr) {
            ca_socket_close(socket);
            cx_allocator_free(alloc, endpoint, sizeof(*endpoint), alignof(*endpoint));
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY, "failed to copy endpoint name");
            }
            return CA_OUT_OF_MEMORY;
        }
        memcpy(endpoint->name, name, len + 1);
    }

    *out = endpoint;
    return CA_OK;
}

ca_status ca_endpoint_listen(ca_endpoint** out, const char* name, int backlog, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = nullptr;

    if (name == nullptr || name[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "name is nullptr or empty");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_socket_addr_t addr;
    ca_status status = ca_socket_addr_un(&addr, name, err);
    if (status != CA_OK) {
        return status;
    }

    ca_socket_t socket = ca_socket_create(CA_SOCKET_DOMAIN_UNIX, CA_SOCKET_TYPE_STREAM, err);
    if (socket == CA_SOCKET_INVALID) {
        return endpoint_error_code(err, CA_ENDPOINT_ERROR);
    }

    status = ca_socket_bind(socket, &addr, err);
    if (status != CA_OK) {
        ca_socket_close(socket);
        return status;
    }

    if (backlog <= 0) {
        backlog = SOMAXCONN;
    }

    status = ca_socket_listen(socket, backlog, err);
    if (status != CA_OK) {
        ca_socket_close(socket);
        (void)ca_fs_remove(name, nullptr);
        return status;
    }

#if !defined(_WIN32) && !defined(__CYGWIN__)
    /* The socket is private to the owning user. Best effort: the bind mode is
     * governed by the process umask, so tighten it explicitly. */
    (void)chmod(name, 0700);
#endif

    status = endpoint_wrap(out, socket, name, true, err);
    if (status != CA_OK) {
        (void)ca_fs_remove(name, nullptr);
    }

    return status;
}

ca_status ca_endpoint_accept(ca_endpoint* server, int timeout_ms, ca_endpoint** out,
                             ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = nullptr;

    if (server == nullptr || !server->server) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "not a listening endpoint");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_status status = ca_socket_wait_readable(server->socket, timeout_ms, err);
    if (status != CA_OK) {
        return status;
    }

    ca_socket_t client = ca_socket_accept(server->socket, nullptr, err);
    if (client == CA_SOCKET_INVALID) {
        return endpoint_error_code(err, CA_ENDPOINT_ERROR);
    }

    return endpoint_wrap(out, client, nullptr, false, err);
}

ca_status ca_endpoint_connect(ca_endpoint** out, const char* name, int timeout_ms, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = nullptr;

    if (name == nullptr || name[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "name is nullptr or empty");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_socket_addr_t addr;
    ca_status status = ca_socket_addr_un(&addr, name, err);
    if (status != CA_OK) {
        return status;
    }

    uint64_t deadline = 0;
    if (timeout_ms >= 0) {
        ca_time_us_t now;
        status = ca_time_now(&now, err);
        if (status != CA_OK) {
            return status;
        }
        deadline = now + (uint64_t)timeout_ms * 1000u;
    }

    for (;;) {
        ca_socket_t socket = ca_socket_create(CA_SOCKET_DOMAIN_UNIX, CA_SOCKET_TYPE_STREAM, err);
        if (socket == CA_SOCKET_INVALID) {
            return endpoint_error_code(err, CA_ENDPOINT_ERROR);
        }

        status = ca_socket_connect(socket, &addr, err);
        if (status == CA_OK) {
            return endpoint_wrap(out, socket, name, false, err);
        }

        ca_socket_close(socket);

        if (status != CA_BUSY) {
            return status;
        }

        uint64_t sleep_us = 10000;
        if (timeout_ms >= 0) {
            ca_time_us_t now;
            status = ca_time_now(&now, err);
            if (status != CA_OK) {
                return status;
            }

            if (now >= deadline) {
                if (err) {
                    ca_error_set(err, CA_TIMED_OUT, "endpoint connect timed out: %s", name);
                }
                return CA_TIMED_OUT;
            }

            uint64_t remaining = deadline - now;
            if (remaining < sleep_us) {
                sleep_us = remaining;
            }
        }

        if (sleep_us == 0) {
            sleep_us = 1;
        }
        (void)ca_time_sleep_us(sleep_us, nullptr);
    }
}

size_t ca_endpoint_read(ca_endpoint* endpoint, void* buffer, size_t size, ca_error* err) {
    if (endpoint == nullptr) {
        if (err) {
            ca_error_init(err);
            ca_error_set(err, CA_INVALID_ARGUMENT, "endpoint is nullptr");
        }
        return (size_t)-1;
    }

    return ca_socket_recv(endpoint->socket, buffer, size, 0, err);
}

size_t ca_endpoint_write(ca_endpoint* endpoint, const void* buffer, size_t size, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (endpoint == nullptr || (buffer == nullptr && size > 0)) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid endpoint or buffer");
        }
        return (size_t)-1;
    }

    const char* bytes = (const char*)buffer;
    size_t total = 0;

    while (total < size) {
        size_t remaining = size - total;
        size_t chunk = remaining;

#if defined(_WIN32) || defined(__CYGWIN__)
        if (chunk > (size_t)INT_MAX) {
            chunk = (size_t)INT_MAX;
        }
#endif

        size_t sent = ca_socket_send(endpoint->socket, bytes + total, chunk, 0, err);
        if (sent == (size_t)-1) {
            return (size_t)-1;
        }

        if (sent == 0) {
            if (err) {
                ca_error_set(err, CA_ENDPOINT_ERROR, "endpoint closed during write");
            }
            return (size_t)-1;
        }

        total += sent;
    }

    return total;
}

ca_status ca_endpoint_flush(ca_endpoint* endpoint, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (endpoint == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "endpoint is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    return CA_OK;
}

void ca_endpoint_close(ca_endpoint** endpoint) {
    if (endpoint == nullptr || *endpoint == nullptr) {
        return;
    }

    ca_endpoint* ep = *endpoint;
    *endpoint = nullptr;

    ca_socket_close(ep->socket);

    if (ep->server && ep->name != nullptr) {
        (void)ca_fs_remove(ep->name, nullptr);
    }

    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, ep->name, ep->name ? strlen(ep->name) + 1 : 0, 1);
    cx_allocator_free(alloc, ep, sizeof(*ep), alignof(*ep));
}

const char* ca_endpoint_name(const ca_endpoint* endpoint) {
    return endpoint != nullptr ? endpoint->name : nullptr;
}

bool ca_endpoint_is_server(const ca_endpoint* endpoint) {
    return endpoint != nullptr && endpoint->server;
}
