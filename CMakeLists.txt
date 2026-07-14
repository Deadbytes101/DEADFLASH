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
if(WIN32)
    target_sources(deadflash_core PRIVATE src/clean.c)
else()
    target_sources(deadflash_core PRIVATE src/clean_stub.c)
endif()

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

    set(DEADFLASH_GUI_ICON
        "${CMAKE_CURRENT_BINARY_DIR}/deadflash-gui.ico")
    set(DEADFLASH_GUI_RC
        "${CMAKE_CURRENT_BINARY_DIR}/deadflash-gui.rc")
    set(DEADFLASH_GUI_MANIFEST
        "${CMAKE_CURRENT_SOURCE_DIR}/src/deadflash-gui.manifest")

    file(TO_CMAKE_PATH "${DEADFLASH_GUI_ICON}" DEADFLASH_GUI_ICON_RC)
    file(TO_CMAKE_PATH "${DEADFLASH_GUI_MANIFEST}" DEADFLASH_GUI_MANIFEST_RC)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/deadflash-gui.rc.in"
        "${DEADFLASH_GUI_RC}"
        @ONLY
    )

    add_custom_command(
        OUTPUT "${DEADFLASH_GUI_ICON}"
        COMMAND powershell.exe
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/materialize-gui-icon.ps1"
            -OutputPath "${DEADFLASH_GUI_ICON}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/materialize-gui-icon.ps1"
        VERBATIM
        COMMENT "Generating DEADFLASH USB flash-drive icon"
    )
    add_custom_target(deadflash_gui_brand
        DEPENDS "${DEADFLASH_GUI_ICON}")

    add_executable(deadflash-gui WIN32
        src/gui_win32_bootstrap.c
        src/gui_brand.c
        "${DEADFLASH_GUI_RC}"
    )
    add_dependencies(deadflash-gui deadflash_gui_brand)
    target_include_directories(deadflash-gui PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(deadflash-gui PRIVATE
        UNICODE
        _UNICODE
        _WIN32_WINNT=0x0601
    )
    if(MSVC)
        target_compile_options(deadflash-gui PRIVATE
            /FIlimits.h
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/gui_brand_preinclude.h"
            /utf-8
        )
        # The configured resource file already embeds RT_MANIFEST resource ID 1.
        # Disable the linker's generated manifest so CVTRES receives one
        # authoritative manifest resource instead of two colliding ID 1 entries.
        target_link_options(deadflash-gui PRIVATE /MANIFEST:NO)
    else()
        target_compile_options(deadflash-gui PRIVATE
            -include "${CMAKE_CURRENT_SOURCE_DIR}/src/gui_brand_preinclude.h")
    endif()
    target_link_libraries(deadflash-gui PRIVATE
        deadflash_core
        comctl32
        comdlg32
        shell32
        uxtheme
    )
    deadflash_enable_warnings(deadflash-gui)
    add_custom_command(TARGET deadflash-gui POST_BUILD
        COMMAND powershell.exe
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-gui-binary.ps1"
            -Path "$<TARGET_FILE:deadflash-gui>"
        VERBATIM
        COMMENT "Validating DEADFLASH GUI PE metadata, manifest, and USB icon"
    )
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

    add_executable(test_clean tests/test_clean.c)
    target_link_libraries(test_clean PRIVATE deadflash_core)
    deadflash_enable_warnings(test_clean)
    add_custom_command(TARGET test_clean POST_BUILD
        COMMAND $<TARGET_FILE:test_clean>
        VERBATIM
        COMMENT "Running fail-closed disk clean contract test"
    )
endif()

install(TARGETS deadflash deadflash-proof RUNTIME DESTINATION bin)
if(WIN32 AND DEADFLASH_BUILD_GUI)
    install(TARGETS deadflash-gui RUNTIME DESTINATION bin)
endif()
