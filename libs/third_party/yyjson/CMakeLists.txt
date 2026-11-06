# yyjson vendored component.
# yyjson is MIT-licensed, single-header/single-source C JSON library.
# Source: https://github.com/ibireme/yyjson

set(YYJSON_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/yyjson.c
)

set(YYJSON_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/src/yyjson.h
)

add_library(yyjson STATIC ${YYJSON_SOURCES} ${YYJSON_HEADERS})

target_include_directories(yyjson PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src)

target_compile_features(yyjson PUBLIC c_std_23)

# yyjson uses no GNU extensions, valid C23 as-is.
target_compile_options(yyjson PRIVATE -Wall -Wextra -Werror)

set_target_properties(yyjson PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(MSVC)
  target_compile_options(yyjson PRIVATE /W4)
endif()
