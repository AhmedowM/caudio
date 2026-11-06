#ifndef CAUDIO_TYPES_API_H
#define CAUDIO_TYPES_API_H

// clang-format off
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
#endif // CAUDIO_API
//clang-format on

#endif // CAUDIO_TYPES_API_H