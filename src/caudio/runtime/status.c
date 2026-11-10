#include <caudio/runtime/status.h>
#include <stdio.h>
#include <string.h>

const char* ca_status_name(ca_status status) {
    switch (status) {
    case CA_OK:
        return "CA_OK";
    case CA_INVALID_ARGUMENT:
        return "CA_INVALID_ARGUMENT";
    case CA_OUT_OF_MEMORY:
        return "CA_OUT_OF_MEMORY";
    case CA_NOT_FOUND:
        return "CA_NOT_FOUND";
    case CA_ALREADY_EXISTS:
        return "CA_ALREADY_EXISTS";
    case CA_IO_ERROR:
        return "CA_IO_ERROR";
    case CA_PERMISSION_DENIED:
        return "CA_PERMISSION_DENIED";
    case CA_TIMED_OUT:
        return "CA_TIMED_OUT";
    case CA_INTERRUPTED:
        return "CA_INTERRUPTED";
    case CA_OVERFLOW:
        return "CA_OVERFLOW";
    case CA_NOT_SUPPORTED:
        return "CA_NOT_SUPPORTED";
    case CA_BUSY:
        return "CA_BUSY";
    case CA_CANCELLED:
        return "CA_CANCELLED";
    case CA_STATE:
        return "CA_STATE";
    case CA_DEVICE:
        return "CA_DEVICE";
    case CA_INTERNAL:
        return "CA_INTERNAL";
    case CA_JSON_ERROR:
        return "CA_JSON_ERROR";
    case CA_PATH_ERROR:
        return "CA_PATH_ERROR";
    case CA_THREAD_ERROR:
        return "CA_THREAD_ERROR";
    case CA_LOCK_ERROR:
        return "CA_LOCK_ERROR";
    case CA_ENDPOINT_ERROR:
        return "CA_ENDPOINT_ERROR";
    case CA_SOCKET_ERROR:
        return "CA_SOCKET_ERROR";
    case CA_SHM_ERROR:
        return "CA_SHM_ERROR";
    case CA_FS_ERROR:
        return "CA_FS_ERROR";
    default:
        return "ca_status_unknown";
    }
}
