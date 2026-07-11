cmake_minimum_required(VERSION 3.20)
project(DEADFLASH VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(DEADFLASH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(DEADFLASH_BUILD_TESTS "Build DEADFLASH tests" ON)
option(DEADFLASH_BUILD_GUI "Build the native Windows GUI" ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

function(deadflash_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
        if(DEADFLASH_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(DEADFLASH_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(deadflash_core STATIC
    src/common.c
    src/sha256.c
    src/device.c
    src/pipeline.c
    src/fat32.c
    src/report.c
    src/attest.c
    src/proof.c
)

target_include_directories(deadflash_core PUBLIC include)
target_compile_definitions(deadflash_core PRIVATE _FILE_OFFSET_BITS=64)
if(NOT WIN32)
    target_compile_definitions(deadflash_core PRIVATE _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()
deadflash_enable_warnings(deadflash_core)

add_executable(deadflash src/main.c)
target_link_libraries(deadflash PRIVATE deadflash_core)
deadflash_enable_warnings(deadflash)

add_executable(deadflash-proof src/proof_main.c)
target_link_libraries(deadflash-proof PRIVATE deadflash_core)
deadflash_enable_warnings(deadflash-proof)

if(WIN32 AND DEADFLASH_BUILD_GUI)
    enable_language(RC)
    add_executable(deadflash-gui WIN32
        src/gui_win32_bootstrap.c
        src/deadflash-gui.rc
    )
    target_compile_definitions(deadflash-gui PRIVATE
        UNICODE
        _UNICODE
        _WIN32_WINNT=0x0601
    )
    if(MSVC)
        target_compile_options(deadflash-gui PRIVATE /FIlimits.h)
    endif()
    target_link_libraries(deadflash-gui PRIVATE
        deadflash_core
        comctl32
        comdlg32
        shell32
    )
    deadflash_enable_warnings(deadflash-gui)
endif()

if(DEADFLASH_BUILD_TESTS)
    enable_testing()

    add_executable(test_sha256 tests/test_sha256.c)
    target_link_libraries(test_sha256 PRIVATE deadflash_core)
    deadflash_enable_warnings(test_sha256)
    add_test(NAME sha256 COMMAND test_sha256)

    add_executable(test_pipeline tests/test_pipeline.c)
    target_link_libraries(test_pipeline PRIVATE deadflash_core)
    deadflash_enable_warnings(test_pipeline)
    add_test(NAME pipeline COMMAND test_pipeline)

    add_executable(test_fat32 tests/test_fat32.c)
    target_link_libraries(test_fat32 PRIVATE deadflash_core)
    deadflash_enable_warnings(test_fat32)
    add_test(NAME fat32 COMMAND test_fat32)

    add_executable(test_identity tests/test_identity.c)
    target_link_libraries(test_identity PRIVATE deadflash_core)
    deadflash_enable_warnings(test_identity)
    add_test(NAME identity COMMAND test_identity)

    add_executable(test_attest tests/test_attest.c)
    target_link_libraries(test_attest PRIVATE deadflash_core)
    deadflash_enable_warnings(test_attest)
    add_test(NAME attest COMMAND test_attest)

    add_executable(test_proof tests/test_proof.c)
    target_link_libraries(test_proof PRIVATE deadflash_core)
    deadflash_enable_warnings(test_proof)
    add_test(NAME proof COMMAND test_proof)

    add_executable(test_fingerprint tests/test_fingerprint.c)
    target_link_libraries(test_fingerprint PRIVATE deadflash_core)
    deadflash_enable_warnings(test_fingerprint)
    add_test(NAME fingerprint COMMAND test_fingerprint)
endif()

install(TARGETS deadflash deadflash-proof RUNTIME DESTINATION bin)
if(WIN32 AND DEADFLASH_BUILD_GUI)
    install(TARGETS deadflash-gui RUNTIME DESTINATION bin)
endif()
