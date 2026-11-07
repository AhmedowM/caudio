# yyjson vendored component.
# yyjson is MIT-licensed, single-header/single-source C JSON library.
# Source: https://github.com/ibireme/yyjson
#
# Built as an OBJECT library: its objects are folded into ca_runtime, so it is
# never part of the installed link interface and does not need to be exported.

set(_yyjson_dir ${PROJECT_SOURCE_DIR}/libs/third_party/yyjson)

add_library(yyjson OBJECT
    ${_yyjson_dir}/src/yyjson.c
    ${_yyjson_dir}/src/yyjson.h
)

target_include_directories(yyjson PUBLIC ${_yyjson_dir}/src)

target_compile_features(yyjson PRIVATE c_std_23)

# ca_json enforces CA_JSON_MAX_DEPTH (include/caudio/runtime/json.h) after
# parsing. yyjson treats its limit as exclusive (depth >= limit fails), so
# allow one extra level here; json.c asserts that the two stay in sync.
set(CAUDIO_YYJSON_DEPTH_LIMIT 129)

target_compile_definitions(yyjson PRIVATE
    YYJSON_READER_DEPTH_LIMIT=${CAUDIO_YYJSON_DEPTH_LIMIT}
    YYJSON_WRITER_DEPTH_LIMIT=${CAUDIO_YYJSON_DEPTH_LIMIT}
)

# Third-party code: keep warnings on, but do not make its warnings fatal.
if(MSVC)
  target_compile_options(yyjson PRIVATE /W3)
else()
  target_compile_options(yyjson PRIVATE -Wall -Wextra)
endif()

set_target_properties(yyjson PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
)

unset(_yyjson_dir)
