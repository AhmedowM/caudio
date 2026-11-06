# ca_runtime component — part of caudio's own tree (not a standalone project).
# Self-contained: no dependency on cstdx. Compiles to its own shared library.

set(CA_RUNTIME_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/status.c
    ${CMAKE_CURRENT_SOURCE_DIR}/error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/path.c
    ${CMAKE_CURRENT_SOURCE_DIR}/time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thread.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mutex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cond.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rwlock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/process.c
    ${CMAKE_CURRENT_SOURCE_DIR}/socket.c
    ${CMAKE_CURRENT_SOURCE_DIR}/shm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/file_lock.c
)

set(CA_RUNTIME_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/status.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/fs.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/path.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/time.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/thread.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/mutex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/cond.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/rwlock.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/tls.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/process.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/socket.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/shm.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/caudio/runtime/file_lock.h
)

if(CA_RUNTIME_BUILD_SHARED)
  add_library(ca_runtime SHARED ${CA_RUNTIME_SOURCES} ${CA_RUNTIME_PUBLIC_HEADERS})
  target_compile_definitions(ca_runtime
      PUBLIC  CAUDIO_SHARED
      PRIVATE CAUDIO_EXPORTS
  )
else()
  add_library(ca_runtime STATIC ${CA_RUNTIME_SOURCES} ${CA_RUNTIME_PUBLIC_HEADERS})
  target_compile_definitions(ca_runtime PRIVATE CAUDIO_EXPORTS)
endif()

add_library(ca_runtime::ca_runtime ALIAS ca_runtime)

target_include_directories(ca_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(ca_runtime PUBLIC c_std_23)

set_target_properties(ca_runtime PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    SOVERSION ${PROJECT_VERSION_MAJOR}
    VERSION ${PROJECT_VERSION}
)

if(MSVC)
  target_compile_options(ca_runtime PRIVATE /W4)
else()
  target_compile_options(ca_runtime PRIVATE -Wall -Wextra -Werror)
endif()

if(WIN32)
  target_link_libraries(ca_runtime PRIVATE ws2_32)
endif()
