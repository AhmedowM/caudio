#ifndef CSTDX_STRING_BUILDER_INTERNAL_H
#define CSTDX_STRING_BUILDER_INTERNAL_H

/**
 * @file string_builder_internal.h
 * @brief Private definition of the opaque cx_string_builder handle.
 */

#include <cstdx/string_builder.h>

#include "string_internal.h"

struct cx_string_builder {
    cx_string string;
};

#endif /* CSTDX_STRING_BUILDER_INTERNAL_H */
