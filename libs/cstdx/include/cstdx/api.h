#ifndef CSTDX_API_H
#define CSTDX_API_H

/**
 * @file api.h
 * @brief Export/visibility macro for the cstdx library.
 *
 * cstdx supports two build modes:
 *   - static  (default): CX_API expands to nothing on Windows;
 *   - shared:            CX_API expands to dllexport/dllimport on Windows.
 *
 * On POSIX the project is built with -fvisibility=hidden, so exported
 * functions must be annotated explicitly with default visibility.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(CSTDX_SHARED)
#    if defined(CSTDX_EXPORTS)
#      define CX_API __declspec(dllexport)
#    else
#      define CX_API __declspec(dllimport)
#    endif
#  else
#    define CX_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define CX_API __attribute__((visibility("default")))
#else
#  define CX_API
#endif

/**
 * @brief Annotate a printf-style variadic function.
 *
 * @param format_index One-based index of the format parameter.
 * @param first_argument One-based index of the first variadic argument.
 *
 * Expands to nothing on compilers without `__attribute__((format))`.
 */
#if defined(__GNUC__) || defined(__clang__)
#  define CX_PRINTF(format_index, first_argument) \
      __attribute__((format(printf, format_index, first_argument)))
#else
#  define CX_PRINTF(format_index, first_argument)
#endif

#endif /* CSTDX_API_H */
