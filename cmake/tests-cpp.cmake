# C++23 differential tests for ca_runtime.
#
# These tests are opt-in because they need a C++ compiler in addition to the C
# toolchain and are not part of the default developer build. They link the
# vendored yyjson OBJECT library directly so the test can use it as the oracle
# for ca_json's wrapper behavior without fetching an external JSON library.
#
# Enable with -DCAUDIO_BUILD_CPP_TESTS=ON.

enable_language(CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(caudio_diff_json
    ${PROJECT_SOURCE_DIR}/tests/cpp/diff_json.cpp
)

# yyjson is an OBJECT library. Linking it gives the test its implementation and
# include directories without making it part of ca_runtime's public interface.
target_link_libraries(caudio_diff_json
    PRIVATE ca_runtime::ca_runtime yyjson
)

target_compile_features(caudio_diff_json PRIVATE cxx_std_23)

# Keep the test TU's view of yyjson.h identical to the vendored build.
target_compile_definitions(caudio_diff_json PRIVATE
    YYJSON_READER_DEPTH_LIMIT=${CAUDIO_YYJSON_DEPTH_LIMIT}
    YYJSON_WRITER_DEPTH_LIMIT=${CAUDIO_YYJSON_DEPTH_LIMIT}
)

if(MSVC)
  target_compile_options(caudio_diff_json PRIVATE /W4)
else()
  target_compile_options(caudio_diff_json PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME caudio.diff.json COMMAND caudio_diff_json)
set_tests_properties(caudio.diff.json PROPERTIES LABELS differential)
