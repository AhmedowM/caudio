/* miniaudio_impl.c -- single translation unit for miniaudio implementation.
 *
 * miniaudio is a header-only library: defining MINIAUDIO_IMPLEMENTATION in
 * exactly one TU emits the full implementation. We isolate it here so it is
 * compiled as part of caudio_vendor (third-party, no -Werror) instead of
 * being pulled into caudio's own TUs where -Werror would turn miniaudio's
 * unused-parameter warnings into fatal errors.
 *
 * output.c includes <miniaudio.h) without MINIAUDIO_IMPLEMENTATION and gets
 * declarations only; the implementation is linked from caudio_vendor.
 */

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
