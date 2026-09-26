# Docs.cmake — Doxygen documentation target (enabled via CAUDIO_BUILD_DOCS=ON).
# How to build docs:
#   cmake -B build -G Ninja -DCAUDIO_BUILD_DOCS=ON
#   cmake --build build --target doc   # generates HTML to build/docs/html
# Requires Doxygen (optional). If not found, configure still succeeds.

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

  # Prefer template docs/Doxyfile.in (CMake @VAR@ substitution) if present
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in")
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
      "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
      @ONLY
    )
    set(_caudio_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
      "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
      @ONLY
    )
    set(_caudio_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
    set(_caudio_doxyfile "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile")
    set(_caudio_doxyfile "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile")
  else()
    message(WARNING "CAUDIO_BUILD_DOCS=ON but no Doxyfile or Doxyfile.in found")
    return()
  endif()

  doxygen_add_docs(doc CONFIG_FILE "${_caudio_doxyfile}" COMMENT "Generating Doxygen docs")
  if(TARGET doc)
    # Doxygen on Windows does not auto-create intermediate dirs for nested HTML_OUTPUT;
    # ensure the build-tree output directory exists (docs stay out of the source tree).
    add_custom_target(doc_mkdir
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/docs/html"
      COMMENT "Preparing docs output directory"
      VERBATIM
    )
    add_dependencies(doc doc_mkdir)
  endif()
endfunction()
