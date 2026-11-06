#ifndef CAUDIO_API_H
#define CAUDIO_API_H

/**
 * @file caudio/api.h
 * @brief Single export macro for every caudio component.
 *
 * All caudio shared libraries (caudio.dll, ca_runtime.dll, ca_db.dll, …)
 * export their public API through CAUDIO_API. Defined as dllexport when
 * building the component DLL, dllimport when consuming it, and
 * default-visibility on ELF targets.
 */

/* clang-format off */
#ifndef CAUDIO_API
    #if defined(_WIN32) || defined(__CYGWIN__)
        #ifdef CAUDIO_EXPORTS
            #define CAUDIO_API __declspec(dllexport)
        #else
            #define CAUDIO_API __declspec(dllimport)
        #endif
    #else
        #define CAUDIO_API __attribute__((visibility("default")))
    #endif
#endif
/* clang-format on */

#endif /* CAUDIO_API_H */
