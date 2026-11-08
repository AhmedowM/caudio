#include <cstdx/expected.h>

cx_expected cx_expected_from_value(void* value) {
    return (cx_expected){.state = CX_EXPECTED_VALUE, .data.value = value};
}

cx_expected cx_expected_from_error(cx_error error) {
    return (cx_expected){.state = CX_EXPECTED_ERROR, .data.error = error};
}

bool cx_expected_has_value(const cx_expected* expected) {
    return expected != nullptr && expected->state == CX_EXPECTED_VALUE;
}

bool cx_expected_has_error(const cx_expected* expected) {
    return expected != nullptr && expected->state == CX_EXPECTED_ERROR;
}

void* cx_expected_value(const cx_expected* expected) {
    return cx_expected_has_value(expected) ? expected->data.value : nullptr;
}

const cx_error* cx_expected_error(const cx_expected* expected) {
    return cx_expected_has_error(expected) ? &expected->data.error : nullptr;
}
