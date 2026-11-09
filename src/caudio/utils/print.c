#include <caudio/utils/print.h>

#include <cstdx/string_view.h>

#include <stdarg.h>
#include <stdio.h>

void ca_print(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
}

void ca_println(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap);
}

void ca_print_stdout(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

void ca_println_stdout(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    va_end(ap);
}

long ca_print_view(FILE* f, cx_string_view s) {
    if (!f || s.data == nullptr) return -1;
    if (s.length == 0) return 0;
    return (long)fwrite(s.data, 1, s.length, f);
}

long ca_println_view(FILE* f, cx_string_view s) {
    long n = ca_print_view(f, s);
    if (n < 0) return n;
    if (fputc('\n', f) != '\n') return -1;
    return n + 1;
}
