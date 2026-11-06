#include <cstdx/result.h>

cx_result cx_result_success(void) {
    return (cx_result){
        .state = CX_RESULT_SUCCESS
    };
}

cx_result cx_result_failure(cx_error error) {
    return (cx_result){
        .state = CX_RESULT_FAILURE,
        .error = error
    };
}

bool cx_result_is_success(const cx_result* result) {
    return result != nullptr && result->state == CX_RESULT_SUCCESS;
}

bool cx_result_is_failure(const cx_result* result) {
    return result != nullptr && result->state == CX_RESULT_FAILURE;
}

const cx_error* cx_result_error(const cx_result* result) {
    return cx_result_is_failure(result) ? &result->error : nullptr;
}
