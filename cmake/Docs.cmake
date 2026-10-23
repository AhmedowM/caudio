# Docs.cmake -- Doxygen documentation target (enabled via CAUDIO_BUILD_DOCS=ON).
# How to build docs:
#   cmake -B build -G Ninja -DCAUDIO_BUILD_DOCS=ON
#   cmake --build build --target doc   # generates HTML to build/docs/docs/html
# Requires Doxygen (optional). If not found, configure still succeeds.
# Single Doxyfile template: docs/Doxyfile.in (configured via configure_file @VAR@).

function(caudio_add_docs)
  find_package(Doxygen OPTIONAL_COMPONENTS dot)
  if(NOT DOXYGEN_FOUND)
    message(STATUS "CAUDIO_BUILD_DOCS=ON but Doxygen not found - docs target will not be available (install Doxygen to build docs)")
    return()
  endif()
  if(DOXYGEN_DOT_FOUND)
    set(DOXYGEN_HAVE_DOT YES)
  else()
    set(DOXYGEN_HAVE_DOT NO)
  endif()

  # Doxyfile template (configured via configure_file @VAR@ substitution)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
    @ONLY
  )
  set(_caudio_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

  doxygen_add_docs(doc CONFIG_FILE "${_caudio_doxyfile}" COMMENT "Generating Doxygen docs")
  if(TARGET doc)
    # Doxygen on Windows does not auto-create intermediate dirs for nested HTML_OUTPUT;
    # ensure the build-tree output directory exists (docs stay out of the source tree).
    # HTML_OUTPUT in docs/Doxyfile.in is "docs/html" relative to OUTPUT_DIRECTORY
    # (= binary dir), so this matches the configured Doxyfile exactly.
    add_custom_target(doc_mkdir
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/docs/html"
      COMMENT "Preparing docs output directory"
      VERBATIM
    )
    add_dependencies(doc doc_mkdir)
  endif()
endfunction()
