cmake_minimum_required(VERSION 3.16)
project(cfw VERSION 0.2.0 LANGUAGES C)

# MSVC has no C23 (nullptr, typeof, ...): fail with one clear message.
if(MSVC)
    message(FATAL_ERROR "MSVC is not supported (no C23). Use MinGW (gcc) or clang.")
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Per-module system dependencies, computed from the actual export set - see SYSTEM_DEPS in
# the generator (tools/export_cfw.py) - so a module this build doesn't carry never contributes
# a dead find_library, and one it does carry can never be silently missing.
find_package(OpenSSL 3.0 REQUIRED)
find_package(CURL 8.13 REQUIRED)
find_package(libwebsockets 4.3 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_library(BROTLIENC_LIBRARY NAMES brotlienc REQUIRED)
find_library(BROTLIDEC_LIBRARY NAMES brotlidec REQUIRED)
find_library(BROTLICOMMON_LIBRARY NAMES brotlicommon REQUIRED)
find_path(BROTLI_INCLUDE_DIR NAMES brotli/encode.h REQUIRED)
find_library(CGLM_LIBRARY NAMES cglm REQUIRED)
find_path(CGLM_INCLUDE_DIR NAMES cglm/cglm.h REQUIRED)
find_library(PCRE2_LIBRARY NAMES pcre2-8 REQUIRED)
find_path(PCRE2_INCLUDE_DIR NAMES pcre2.h REQUIRED)

add_compile_definitions(_GNU_SOURCE ARENA_IMPLEMENTATION ERROR_CHECK_ENABLED LOG_THREAD_IMPLEMENTATION
                        MEMORY_HOOKS_IMPLEMENTATION MEMORY_NON_DANGLING_POINTER TRACELOG_ENABLED)
include_directories("${CMAKE_SOURCE_DIR}/include" "${OPENSSL_INCLUDE_DIR}" "${CURL_INCLUDE_DIRS}" "${LIBWEBSOCKETS_INCLUDE_DIRS}" "${ZLIB_INCLUDE_DIRS}" "${BROTLI_INCLUDE_DIR}" "${CGLM_INCLUDE_DIR}" "${PCRE2_INCLUDE_DIR}")

file(GLOB_RECURSE CFW_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/include/*.c")
list(FILTER CFW_SRC EXCLUDE REGEX "/include/test/")
add_library(cfw STATIC ${CFW_SRC})
target_link_libraries(cfw PUBLIC OpenSSL::Crypto CURL::libcurl websockets_shared ZLIB::ZLIB ${BROTLIENC_LIBRARY} ${BROTLIDEC_LIBRARY} ${BROTLICOMMON_LIBRARY} ${CGLM_LIBRARY} ${PCRE2_LIBRARY} m)
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)   # CFW's windows.h asserts this baseline
    target_link_libraries(cfw PUBLIC ws2_32)                     # Winsock, wrapped by platform/windows
else()
    target_link_libraries(cfw PUBLIC pthread)
endif()

enable_testing()
file(GLOB_RECURSE TEST_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*/test_*.c")
foreach(src ${TEST_SRC})
    get_filename_component(name "${src}" NAME_WE)
    if(name STREQUAL "test_oom")
        continue()   # --wrap / arena-exhaustion harness: `make test-oom` under GNU ld
    endif()
    if(name STREQUAL "test_unchecked")
        continue()   # built WITHOUT ERROR_CHECK_ENABLED against its own archive: `make test-unchecked`
    endif()
    file(RELATIVE_PATH rel "${CMAKE_SOURCE_DIR}/tests" "${src}")
    get_filename_component(suite "${rel}" DIRECTORY)
    if(NOT WIN32 AND suite STREQUAL "platform/windows")
        continue()   # Winsock/Win32 suite; the module body is #ifdef _WIN32 elsewhere
    endif()
    string(REPLACE "/" "_" target "${suite}_${name}")
    get_filename_component(suite_dir "${src}" DIRECTORY)
    file(GLOB helpers CONFIGURE_DEPENDS "${suite_dir}/*.c")
    list(FILTER helpers EXCLUDE REGEX "/test_[^/]*\.c$")
    # A suite that borrows another suite's fixture rather than carrying a second copy of it.
    # The glob above only ever sees the suite's own directory, so the borrowed sources and
    # include directory are named here, generated from the export's own borrowing map.
    set(borrowed_includes "")
    if(suite STREQUAL "http/service/captcha")
        list(APPEND helpers "${CMAKE_SOURCE_DIR}/tests/http/client/fixture.c")
        list(APPEND borrowed_includes "${CMAKE_SOURCE_DIR}/tests/http/client")
    endif()
    add_executable(${target} "${src}" ${helpers} "${CMAKE_SOURCE_DIR}/include/test/test.c")
    target_include_directories(${target} PRIVATE "${suite_dir}")
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/tests/${suite}")
    foreach(borrowed ${borrowed_includes})
        target_include_directories(${target} PRIVATE "${borrowed}")
    endforeach()
    target_link_libraries(${target} cfw)
    add_test(NAME ${target} COMMAND ${target})
endforeach()
