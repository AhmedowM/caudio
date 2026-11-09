#include <caudio/utils/math.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <caudio/runtime/error.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Convert a cx_status to a ca_status. */
static ca_status ca_from_cx(cx_status status) {
    switch (status) {
    case CX_OK:             return CA_OK;
    case CX_OUT_OF_MEMORY:  return CA_OUT_OF_MEMORY;
    case CX_INVALID_ARGUMENT: return CA_INVALID_ARGUMENT;
    case CX_OVERFLOW:       return CA_OVERFLOW;
    case CX_OUT_OF_RANGE:   return CA_INVALID_ARGUMENT;
    default:                return CA_INVALID_ARGUMENT;
    }
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

ca_status ca_to_hex(const uint8_t bytes[32],
                    const cx_allocator* alloc,
                    cx_string** out) {
    if (!bytes || !out) {
        if (out) *out = nullptr;
        return CA_INVALID_ARGUMENT;
    }
    const cx_allocator* resolved = alloc ? alloc : cx_allocator_default();
    cx_string* s = nullptr;
    ca_status st = ca_from_cx(cx_string_init(&s, resolved));
    if (st != CA_OK) {
        if (out) *out = nullptr;
        return st;
    }
    static const char digits[] = "0123456789abcdef";
    char buf[65];
    for (int i = 0; i < 32; i++) {
        buf[i * 2]     = digits[(bytes[i] >> 4) & 0x0f];
        buf[i * 2 + 1] = digits[bytes[i] & 0x0f];
    }
    buf[64] = '\0';
    st = ca_from_cx(cx_string_append_cstr(s, buf));
    if (st != CA_OK) {
        cx_string_destroy(&s);
        if (out) *out = nullptr;
        return st;
    }
    *out = s;
    return CA_OK;
}

bool ca_from_hex(cx_string_view hex_str, uint8_t out[32]) {
    if (!out) return false;
    if (hex_str.length != 64) return false;
    const char* p = hex_str.data;
    for (int i = 0; i < 32; i++) {
        int hi = hex_value(p[i * 2]);
        int lo = hex_value(p[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

ca_status ca_format_clock(double secs,
                          const cx_allocator* alloc,
                          cx_string** out) {
    if (!out) return CA_INVALID_ARGUMENT;
    *out = nullptr;
    if (secs < 0.0) secs = 0.0;
    long total = (long)secs;
    char buf[32];
    int n;
    long h = total / 3600;
    if (h > 0) {
        n = snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld",
                     h, (total % 3600) / 60, total % 60);
    } else {
        n = snprintf(buf, sizeof(buf), "%02ld:%02ld",
                     (total % 3600) / 60, total % 60);
    }
    if (n < 0 || (size_t)n >= sizeof(buf)) return CA_OVERFLOW;
    const cx_allocator* resolved = alloc ? alloc : cx_allocator_default();
    cx_string* s = nullptr;
    ca_status st = ca_from_cx(cx_string_init(&s, resolved));
    if (st != CA_OK) return st;
    st = ca_from_cx(cx_string_append_cstr(s, buf));
    if (st != CA_OK) {
        cx_string_destroy(&s);
        return st;
    }
    *out = s;
    return CA_OK;
}
