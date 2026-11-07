# caudio sanitizer configuration.
#
# caudio applies its own sanitizer option once, at directory scope, before any
# targets (including the in-tree cstdx subdirectory) are created. This keeps
# every object that ends up in the same binary instrumented consistently.
# caudio deliberately never reads cstdx's sanitizer option; cstdx remains an
# independently buildable project with its own CSTDX_SANITIZERS knob.

set(CAUDIO_SANITIZERS "" CACHE STRING
    "Sanitizers to enable, comma-separated: address,undefined,thread,memory,leak \
(asan/ubsan/tsan/msan aliases are accepted)")

string(REPLACE ";" "," _caudio_sanitizers "${CAUDIO_SANITIZERS}")
string(TOLOWER "${_caudio_sanitizers}" _caudio_sanitizers)
string(REPLACE " " "" _caudio_sanitizers "${_caudio_sanitizers}")

if(_caudio_sanitizers)
  string(REPLACE "," ";" _caudio_sanitizer_list "${_caudio_sanitizers}")
  set(_caudio_sanitizer_ids "")
  foreach(_sanitizer IN LISTS _caudio_sanitizer_list)
    if(_sanitizer STREQUAL "")
      continue()
    endif()

    if(_sanitizer STREQUAL "asan")
      set(_sanitizer address)
    elseif(_sanitizer STREQUAL "ubsan")
      set(_sanitizer undefined)
    elseif(_sanitizer STREQUAL "tsan")
      set(_sanitizer thread)
    elseif(_sanitizer STREQUAL "msan")
      set(_sanitizer memory)
    endif()

    if(NOT _sanitizer MATCHES "^(address|undefined|thread|memory|leak)$")
      message(FATAL_ERROR
          "CAUDIO_SANITIZERS: unsupported sanitizer '${_sanitizer}' "
          "(expected address, undefined, thread, memory or leak)")
    endif()

    list(APPEND _caudio_sanitizer_ids "${_sanitizer}")
  endforeach()
  list(REMOVE_DUPLICATES _caudio_sanitizer_ids)
  list(JOIN _caudio_sanitizer_ids "," _caudio_sanitizer_arg)

  if(MSVC)
    if(NOT _caudio_sanitizer_arg STREQUAL "address")
      message(FATAL_ERROR "CAUDIO_SANITIZERS: MSVC only supports 'address'")
    endif()
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    set(_caudio_sanitize_flags
        "-fsanitize=${_caudio_sanitizer_arg}"
        -fno-omit-frame-pointer
        -g
    )
    if(_caudio_sanitizer_arg MATCHES "undefined")
      list(APPEND _caudio_sanitize_flags -fno-sanitize-recover=all)
    endif()
    add_compile_options(${_caudio_sanitize_flags})
    add_link_options("-fsanitize=${_caudio_sanitizer_arg}")
  endif()

  message(STATUS "caudio sanitizers: ${_caudio_sanitizer_arg}")
endif()
unset(_caudio_sanitizers)
unset(_caudio_sanitizer_list)
unset(_caudio_sanitizer_ids)
unset(_caudio_sanitizer_arg)
unset(_caudio_sanitize_flags)
unset(_sanitizer)
