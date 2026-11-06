#include <cstdx/status.h>

static_assert(sizeof(cx_status) == 1);

const char *cx_status_name(cx_status status) {
    switch (status) {
        case CX_OK:               return "CX_OK";
        case CX_INVALID_ARGUMENT: return "CX_INVALID_ARGUMENT";
        case CX_OUT_OF_MEMORY:    return "CX_OUT_OF_MEMORY";
        case CX_OVERFLOW:         return "CX_OVERFLOW";
        case CX_OUT_OF_RANGE:     return "CX_OUT_OF_RANGE";
        case CX_INVALID_STATE:    return "CX_INVALID_STATE";
        case CX_NOT_FOUND:        return "CX_NOT_FOUND";
        case CX_UNSUPPORTED:      return "CX_UNSUPPORTED";
        case CX_IO:               return "CX_IO";
        case CX_INTERNAL:         return "CX_INTERNAL";
    }

    return "CX_UNKNOWN";
}
