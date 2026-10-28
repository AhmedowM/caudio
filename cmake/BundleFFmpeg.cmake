# Bundle FFmpeg runtime DLLs/dylibs next to the executable so CPack archives
# run standalone. (macOS bundles behind CAUDIO_BUNDLE_FFMPEG_MACOS;
# Linux is bundled unconditionally below.)

if(WIN32 AND CAUDIO_BUILD_CLI)
  # Our FindFFmpeg targets are UNKNOWN IMPORTED (no runtime location), so
  # resolve the DLLs by layout: <root>/bin next to the found import library
  # (gyan/choco/vcpkg layouts).
  get_filename_component(_ff_libdir "${FFmpeg_AVCODEC_LIBRARY}" DIRECTORY)
  file(GLOB _ff_dlls
    "${_ff_libdir}/../bin/avcodec-*.dll"
    "${_ff_libdir}/../bin/avformat-*.dll"
    "${_ff_libdir}/../bin/avutil-*.dll"
    "${_ff_libdir}/../bin/*swresample-*.dll")
  if(_ff_dlls)
    install(FILES ${_ff_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
  else()
    message(WARNING "FFmpeg DLLs not found near ${FFmpeg_AVCODEC_LIBRARY} -- packages will need them on PATH")
  endif()
  if(MINGW)
    # Only winpthread ships as a DLL: libstdc++/libgcc are statically linked
    # into the exe above. winpthread stays shared on purpose -- FFmpeg's own
    # DLLs use it too, and two pthread runtimes in one process is worse than
    # one shared DLL.
    get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB _mingw_rt_dlls "${_mingw_bin}/libwinpthread-1.dll")
    if(_mingw_rt_dlls)
      install(FILES ${_mingw_rt_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    else()
      message(WARNING "libwinpthread-1.dll not found next to ${CMAKE_CXX_COMPILER} -- packages will need it on PATH")
    endif()
  endif()
endif()

if(UNIX AND NOT APPLE AND CAUDIO_BUILD_CLI)
  # Ship the versioned .so files (SONAME links included -- the loader asks for
  # libavcodec.so.62 by exact name) in lib/. RPATHs are cmake/Install.cmake's
  # job (target properties, not CMAKE_INSTALL_RPATH: the variable is consumed
  # at target creation and this file runs after the targets exist).
  get_filename_component(_ff_libdir "${FFmpeg_AVCODEC_LIBRARY}" DIRECTORY)
  set(_ff_sos "")
  foreach(_lib avcodec avformat avutil swresample)
    file(GLOB _cand "${_ff_libdir}/lib${_lib}.so.*")
    list(APPEND _ff_sos ${_cand})
  endforeach()
  list(REMOVE_DUPLICATES _ff_sos)
  if(NOT _ff_sos)
    if(NOT FFmpeg_AVCODEC_LIBRARY MATCHES "\\.a$")
      message(WARNING "FFmpeg shared libs not found near ${FFmpeg_AVCODEC_LIBRARY} -- packages will need them on the loader path")
    endif()
  else()
    install(FILES ${_ff_sos} DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime)
  endif()
endif()

if(APPLE AND CAUDIO_BUILD_CLI AND CAUDIO_BUNDLE_FFMPEG_MACOS)
  # Bundle versioned dylibs in lib/ and rewire ids + references to @rpath and
  # re-sign ad-hoc (install_name_tool invalidates signatures; without re-sign
  # the kernel refuses the binaries on Apple Silicon).
  get_filename_component(_ff_libdir "${FFmpeg_AVCODEC_LIBRARY}" DIRECTORY)
  set(_ff_dylibs "")
  foreach(_lib avcodec avformat avutil swresample)
    # Bundle versioned files AND SONAME links: the loader asks for the
    # SONAME by exact name (only the bare dev link stays out, via the glob).
    file(GLOB _cand "${_ff_libdir}/lib${_lib}.*.dylib")
    list(APPEND _ff_dylibs ${_cand})
  endforeach()
  list(REMOVE_DUPLICATES _ff_dylibs)
  if(NOT _ff_dylibs)
    message(WARNING "CAUDIO_BUNDLE_FFMPEG_MACOS requested but no versioned FFmpeg dylibs near ${FFmpeg_AVCODEC_LIBRARY}")
  else()
    install(FILES ${_ff_dylibs} DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime)
    set(_ff_dylib_names "")
    foreach(_f IN LISTS _ff_dylibs)
      get_filename_component(_n "${_f}" NAME)
      list(APPEND _ff_dylib_names "${_n}")
    endforeach()
    # Exe file name resolved at configure time: genexes are NOT evaluated
    # inside install(CODE).
    get_target_property(_cli_out caudio_cli OUTPUT_NAME)
    if(_cli_out STREQUAL "_cli_out-NOTFOUND")
      set(_cli_out caudio_cli)
    endif()
    install(CODE "
      set(_exe \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${_cli_out}\")
      set(_libdir \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\")
      set(_bins \"\${_exe}\")
      foreach(_n ${_ff_dylib_names})
        list(APPEND _bins \"\${_libdir}/\${_n}\")
        if(IS_SYMLINK \"\${_libdir}/\${_n}\")
          continue()
        endif()
        execute_process(COMMAND install_name_tool -id \"@rpath/\${_n}\" \"\${_libdir}/\${_n}\" RESULT_VARIABLE _rc_id OUTPUT_VARIABLE _id_out ERROR_VARIABLE _id_err)
        if(NOT \"\${_rc_id}\" STREQUAL \"0\")
          message(FATAL_ERROR \"FFmpeg bundle: install_name_tool -id failed for \${_n} (exit \${_rc_id})\\n\${_id_out}\\n\${_id_err}\")
        endif()
      endforeach()
      # Every installed own dylib too, not just the executable: a component
      # library that links brew FFmpeg (media) keeps the build-host path in its
      # load commands otherwise, which breaks relocating the runtime archive.
      file(GLOB _own \"\${_libdir}/*.dylib\")
      list(APPEND _bins \${_own})
      list(REMOVE_DUPLICATES _bins)
      foreach(_b IN LISTS _bins)
        # Links share their target's load commands (already handled via the
        # real file): processing both would re-change @rpath refs onto
        # themselves and re-sign twice.
        if(IS_SYMLINK \"\${_b}\")
          continue()
        endif()
        execute_process(COMMAND otool -L \"\${_b}\" OUTPUT_VARIABLE _refs OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_VARIABLE _ot_err RESULT_VARIABLE _rc_ot)
        if(NOT \"\${_rc_ot}\" STREQUAL \"0\")
          execute_process(COMMAND file \"\${_b}\" OUTPUT_VARIABLE _file_info OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
          message(FATAL_ERROR \"FFmpeg bundle: otool -L failed for \${_b} (exit \${_rc_ot})\\nfile: \${_file_info}\\n\${_ot_err}\")
        endif()
        foreach(_n ${_ff_dylib_names})
          # _old keeps the FULL old path: -change matches load commands
          # exactly, and a bare filename silently no-ops.
          string(REGEX MATCH \"[^ \\t\\r\\n]*/\${_n}( |\$)\" _hit \"\${_refs}\")
          if(_hit)
            string(STRIP \"\${_hit}\" _old)
            execute_process(COMMAND install_name_tool -change \"\${_old}\" \"@rpath/\${_n}\" \"\${_b}\" RESULT_VARIABLE _rc_ch OUTPUT_QUIET ERROR_QUIET)
            if(NOT _rc_ch EQUAL 0)
              message(FATAL_ERROR \"FFmpeg bundle: install_name_tool -change failed: \${_old} in \${_b}\")
            endif()
          endif()
        endforeach()
        execute_process(COMMAND codesign --force -s - \"\${_b}\" RESULT_VARIABLE _rc_cs OUTPUT_QUIET ERROR_QUIET)
        if(NOT _rc_cs EQUAL 0)
          message(FATAL_ERROR \"FFmpeg bundle: ad-hoc codesign failed for \${_b}\")
        endif()
      endforeach()
    " COMPONENT runtime)
  endif()
endif()

unset(_ff_libdir)
unset(_ff_dlls)
unset(_mingw_bin)
unset(_mingw_rt_dlls)
unset(_ff_sos)
unset(_cand)
unset(_cli_rpath)
unset(_ff_dylib_names)
unset(_f)
unset(_n)
