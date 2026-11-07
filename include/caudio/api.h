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
 *
 * CAUDIO_SHARED is defined (PUBLIC) by the build when the component is a
 * shared library. Static builds leave it undefined so consumers never see
 * dllimport.
 */

/* clang-format off */
#ifndef CAUDIO_API
    #if defined(_WIN32) || defined(__CYGWIN__)
        #if defined(CAUDIO_SHARED)
            #ifdef CAUDIO_EXPORTS
                #define CAUDIO_API __declspec(dllexport)
            #else
                #define CAUDIO_API __declspec(dllimport)
            #endif
        #else
            #define CAUDIO_API
        #endif
    #elif defined(__GNUC__) || defined(__clang__)
        #define CAUDIO_API __attribute__((visibility("default")))
    #else
        #define CAUDIO_API
    #endif
#endif
/* clang-format on */

/**
 * @brief Annotate a printf-style variadic function.
 *
 * @param format_index One-based index of the format parameter.
 * @param first_argument One-based index of the first variadic argument.
 *
 * Expands to nothing on compilers without `__attribute__((format))`.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define CA_PRINTF(format_index, first_argument) \
        __attribute__((format(printf, format_index, first_argument)))
#else
    #define CA_PRINTF(format_index, first_argument)
#endif

#endif /* CAUDIO_API_H */
