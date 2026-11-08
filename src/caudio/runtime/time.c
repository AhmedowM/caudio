#include <caudio/runtime/time.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <time.h>
    #include <errno.h>
#endif

ca_status ca_time_now(ca_time_us_t *out, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    if (!QueryPerformanceFrequency(&frequency)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR,
                         "QueryPerformanceFrequency failed");
        }
        return CA_IO_ERROR;
    }

    if (!QueryPerformanceCounter(&counter)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "QueryPerformanceCounter failed");
        }
        return CA_IO_ERROR;
    }

    /* Microseconds = counter.QuadPart * 1,000,000 / frequency.QuadPart */
    /* Use unsigned __int64 math to avoid overflow on 32-bit builds. */
    unsigned __int64 usec =
        (unsigned __int64)counter.QuadPart * 1000000ULL /
        (unsigned __int64)frequency.QuadPart;

    *out = (ca_time_us_t)usec;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "clock_gettime failed: %d", errno);
        }
        return CA_IO_ERROR;
    }

    *out = (ca_time_us_t)ts.tv_sec * 1000000ULL +
           (ca_time_us_t)ts.tv_nsec / 1000ULL;
#endif

    return CA_OK;
}

ca_status ca_time_sleep_us(ca_time_us_t microseconds, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Sleep takes milliseconds; round up. */
    DWORD ms = (DWORD)((microseconds + 999ULL) / 1000ULL);
    if (ms == 0) {
        ms = 1;
    }
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(microseconds / 1000000ULL);
    req.tv_nsec = (long)((microseconds % 1000000ULL) * 1000ULL);

    while (nanosleep(&req, &req) != 0) {
        if (errno != EINTR) {
            if (err) {
                ca_error_set(err, CA_IO_ERROR, "nanosleep failed: %d", errno);
            }
            return CA_IO_ERROR;
        }
        /* Interrupted by a signal; req is updated with remaining time. */
    }
#endif

    return CA_OK;
}
