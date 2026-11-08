#include <caudio/runtime/error.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ca_error_init(ca_error* err) {
    if (err == nullptr) {
        return;
    }

    err->code = CA_OK;
    err->message[0] = '\0';
}

void ca_error_set(ca_error* err, ca_status code, const char* fmt, ...) {
    if (err == nullptr) {
        return;
    }

    err->code = code;

    if (fmt == nullptr) {
        err->message[0] = '\0';
        return;
    }

    va_list args;
    va_start(args, fmt);

    /* Try to write into the fixed buffer; vsnprintf always NUL-terminates. */
    int written = vsnprintf(err->message, sizeof(err->message), fmt, args);
    va_end(args);

    if (written < 0) {
        err->message[0] = '\0';
        return;
    }

    /* Truncate safely if the message did not fit. */
    if ((size_t)written >= sizeof(err->message)) {
        err->message[sizeof(err->message) - 1] = '\0';
    }
}
