cmake_minimum_required(VERSION 3.16)
project(cfw VERSION 0.1.0 LANGUAGES C)

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


add_compile_definitions(_GNU_SOURCE ARENA_IMPLEMENTATION ERROR_CHECK_ENABLED LOG_THREAD_IMPLEMENTATION
                        MEMORY_HOOKS_IMPLEMENTATION MEMORY_NON_DANGLING_POINTER TRACELOG_ENABLED)
include_directories("${CMAKE_SOURCE_DIR}/include" )

file(GLOB_RECURSE CFW_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/include/*.c")
list(FILTER CFW_SRC EXCLUDE REGEX "/include/test/")
add_library(cfw STATIC ${CFW_SRC})
target_link_libraries(cfw PUBLIC  m)
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
        continue()   # --wrap harness: `make test-oom` under GNU ld
    endif()
    file(RELATIVE_PATH rel "${CMAKE_SOURCE_DIR}/tests" "${src}")
    get_filename_component(suite "${rel}" DIRECTORY)
    if(NOT WIN32 AND suite STREQUAL "platform/windows")
        continue()   # Winsock/Win32 suite; the module body is #ifdef _WIN32 elsewhere
    endif()
    string(REPLACE "/" "_" target "${suite}_${name}")
    add_executable(${target} "${src}" "${CMAKE_SOURCE_DIR}/include/test/test.c")
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/tests/${suite}")
    target_link_libraries(${target} cfw)
    add_test(NAME ${target} COMMAND ${target})
endforeach()
