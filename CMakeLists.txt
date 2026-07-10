cmake_minimum_required(VERSION 3.20)
project(DEADFLASH VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(DEADFLASH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(DEADFLASH_BUILD_TESTS "Build DEADFLASH tests" ON)

add_library(deadflash_core STATIC
    src/common.c
    src/sha256.c
    src/io.c
    src/fat32.c
    src/report.c
)

target_include_directories(deadflash_core PUBLIC include)
target_compile_definitions(deadflash_core PRIVATE _FILE_OFFSET_BITS=64)
if(NOT WIN32)
    target_compile_definitions(deadflash_core PRIVATE _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()

if(MSVC)
    target_compile_options(deadflash_core PRIVATE /W4 /permissive-)
    if(DEADFLASH_WARNINGS_AS_ERRORS)
        target_compile_options(deadflash_core PRIVATE /WX)
    endif()
else()
    target_compile_options(deadflash_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(DEADFLASH_WARNINGS_AS_ERRORS)
        target_compile_options(deadflash_core PRIVATE -Werror)
    endif()
endif()

add_executable(deadflash src/main.c)
target_link_libraries(deadflash PRIVATE deadflash_core)
if(MSVC)
    target_compile_options(deadflash PRIVATE /W4 /permissive-)
    if(DEADFLASH_WARNINGS_AS_ERRORS)
        target_compile_options(deadflash PRIVATE /WX)
    endif()
else()
    target_compile_options(deadflash PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(DEADFLASH_WARNINGS_AS_ERRORS)
        target_compile_options(deadflash PRIVATE -Werror)
    endif()
endif()

if(DEADFLASH_BUILD_TESTS)
    enable_testing()
    add_executable(test_sha256 tests/test_sha256.c)
    target_link_libraries(test_sha256 PRIVATE deadflash_core)
    add_test(NAME sha256 COMMAND test_sha256)

    add_executable(test_pipeline tests/test_pipeline.c)
    target_link_libraries(test_pipeline PRIVATE deadflash_core)
    add_test(NAME pipeline COMMAND test_pipeline)

    add_executable(test_fat32 tests/test_fat32.c)
    target_link_libraries(test_fat32 PRIVATE deadflash_core)
    add_test(NAME fat32 COMMAND test_fat32)
endif()

install(TARGETS deadflash RUNTIME DESTINATION bin)
