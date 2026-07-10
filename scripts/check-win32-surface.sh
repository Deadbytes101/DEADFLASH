#!/usr/bin/env bash
set -euo pipefail

cc=${CC:-clang}
common_sources=(
    src/common.c
    src/sha256.c
    src/device.c
    src/pipeline.c
    src/fat32.c
    src/report.c
    src/attest.c
    src/proof.c
    src/main.c
    src/proof_main.c
    tests/test_sha256.c
    tests/test_pipeline.c
    tests/test_fat32.c
    tests/test_identity.c
    tests/test_attest.c
    tests/test_proof.c
    tests/test_fingerprint.c
)

gnu_flags=(
    -std=c17
    -D_WIN32
    -D_CRT_SECURE_NO_WARNINGS
    -Itests/win32-stubs
    -Iinclude
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Werror
    -fsyntax-only
)

for source in "${common_sources[@]}"; do
    printf 'WIN32-GNU-SURFACE %s\n' "$source"
    "$cc" "${gnu_flags[@]}" "$source"
done

cl_flags=(
    --driver-mode=cl
    --target=x86_64-unknown-linux-gnu
    /nologo
    /std:c17
    /W4
    /WX
    /D_WIN32
    /D_CRT_SECURE_NO_WARNINGS
    /Itests/win32-stubs
    /Iinclude
    /Zs
)

for source in "${common_sources[@]}"; do
    printf 'WIN32-CL-SURFACE %s\n' "$source"
    "$cc" "${cl_flags[@]}" "$source"
done
