#include <cstdx/checked.h>
#include <stdint.h>

#if __has_include(<stdckdint.h>)
#include <stdckdint.h>
#define CX_HAS_STDCKDINT 1
#else
#define CX_HAS_STDCKDINT 0
#endif

#if __has_include(<stdbit.h>)
#include <stdbit.h>
#define CX_HAS_STDBIT 1
#else
#define CX_HAS_STDBIT 0
#endif

bool cx_add_overflow(size_t a, size_t b, size_t* out) {
#if CX_HAS_STDCKDINT
    return ckd_add(out, a, b);
#else
    if (a > SIZE_MAX - b) {
        return true;
    }

    *out = a + b;
    return false;
#endif
}

bool cx_mul_overflow(size_t a, size_t b, size_t* out) {
#if CX_HAS_STDCKDINT
    return ckd_mul(out, a, b);
#else
    if (a != 0 && b > SIZE_MAX / a) {
        return true;
    }

    *out = a * b;
    return false;
#endif
}

bool cx_is_power_of_two(size_t value) {
#if CX_HAS_STDBIT
    return stdc_has_single_bit(value);
#else
    /* MinGW-w64 provides <stdckdint.h> but not <stdbit.h> as of GCC 16. */
    return value != 0 && (value & (value - 1)) == 0;
#endif
}

bool cx_align_up(size_t value, size_t alignment, size_t* out) {
    if (!cx_is_power_of_two(alignment)) {
        return false;
    }

    auto mask = alignment - 1;
    size_t sum;

    if (cx_add_overflow(value, mask, &sum)) {
        return false;
    }

    *out = sum & ~mask;
    return true;
}
