# Tests -- Catch2 via system package or FetchContent (144 tests:
# utils/player/db/engine/ipc/cli paths).
if(CAUDIO_ENABLE_TESTS)
  find_package(Catch2 QUIET)
  if(NOT Catch2_FOUND)
    # Keep Catch2 out of our install/packages (cf. JSON_Install=OFF above).
    set(CATCH_INSTALL_DOCS OFF)
    set(CATCH_INSTALL_EXTRAS OFF)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.16.0
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
  else()
    message(STATUS "Catch2 found (system): tests link Catch2::Catch2WithMain")
  endif()
  include(CTest)
  include(Catch)
  enable_testing()

  if(CAUDIO_TEST_NOAUDIO)
    add_compile_definitions(CAUDIO_TEST_NOAUDIO_BUILD=1)
  endif()

  function(caudio_add_catch_test name src)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "WITH_TEST_DATA;WITH_ALLOW_MULTIPLE_DEFINITION" "TEST_CLI_BIN" "LIBS")
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE ${ARG_LIBS} Catch2::Catch2WithMain)
    # + src/: white-box tests cover src-internal helpers (engine detail fns)
    # directly. Build-tree only -- src/ is never installed.
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(${name} SYSTEM PRIVATE vendor)
    target_compile_features(${name} PRIVATE cxx_std_23)
    caudio_apply_extra_flags(${name})
    if(ARG_WITH_TEST_DATA)
      target_compile_definitions(${name} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    endif()
    if(ARG_TEST_CLI_BIN)
      target_compile_definitions(${name} PRIVATE TEST_CLI_BIN="${ARG_TEST_CLI_BIN}")
    endif()
    if(ARG_WITH_ALLOW_MULTIPLE_DEFINITION)
      target_link_options(${name} PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wl,--allow-multiple-definition>)
    endif()
    if(CAUDIO_ENABLE_SANITIZERS)
      caudio_apply_sanitizers(${name})
    endif()
    catch_discover_tests(${name} PROPERTIES SKIP_RETURN_CODE 4)
  endfunction()

  caudio_add_catch_test(test_utils_result tests/test_utils_result.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_ring tests/test_utils_ring.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_queue tests/test_utils_queue.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_thread tests/test_utils_thread.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_log tests/test_utils_log.cpp LIBS caudio::utils)

  caudio_add_catch_test(test_reader tests/test_reader.cpp LIBS caudio::player caudio::utils)
  caudio_add_catch_test(test_decoder tests/test_decoder.cpp LIBS caudio::player caudio::utils WITH_TEST_DATA)
  caudio_add_catch_test(test_ffmpeg tests/test_ffmpeg.cpp LIBS caudio::player caudio::utils WITH_TEST_DATA)

  caudio_add_catch_test(test_db tests/test_db.cpp LIBS caudio::db caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_write_thread tests/test_write_thread.cpp LIBS caudio::db caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_output tests/test_output.cpp LIBS caudio::player caudio::utils)

  caudio_add_catch_test(test_engine_queue tests/test_engine_queue.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_engine_history tests/test_engine_history.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_engine_events tests/test_engine_events.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_engine_state tests/test_engine_state.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_engine_gapless tests/test_engine_gapless.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)

  caudio_add_catch_test(test_db_scan tests/test_db_scan.cpp LIBS caudio::db caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_tag_write tests/test_tag_write.cpp LIBS caudio::service caudio::db caudio::player caudio::utils caudio::sqlite3 WITH_TEST_DATA)
  caudio_add_catch_test(test_db_search tests/test_db_search.cpp LIBS caudio::db caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_db_json tests/test_db_json.cpp LIBS caudio::db caudio::utils caudio::sqlite3)

  caudio_add_catch_test(test_player_integration tests/test_player_integration.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3 WITH_TEST_DATA)

  caudio_add_catch_test(test_ipc tests/test_ipc.cpp LIBS caudio::service caudio::client caudio::ipc caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3 WITH_ALLOW_MULTIPLE_DEFINITION)
  caudio_add_catch_test(test_config tests/test_config.cpp LIBS caudio::config caudio::utils)

  # CLI golden tests: spawn the real executable with hermetic temp configs
  # and assert on stdout/stderr/exit codes (help texts, parse errors,
  # no-daemon errors, full daemon lifecycle). Needs the CLI binary, so it
  # only exists when CAUDIO_BUILD_CLI is on.
  if(TARGET caudio_cli)
    caudio_add_catch_test(test_cli_golden tests/test_cli_golden.cpp LIBS Catch2::Catch2WithMain WITH_TEST_DATA TEST_CLI_BIN "$<TARGET_FILE:caudio_cli>")
  endif()

  # D-4: compile every public header as a standalone TU (build gate, no ctest
  # entry -- the objects are never linked, only compiled).
  caudio_add_header_self_containment()
endif()
