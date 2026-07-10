#!/usr/bin/env bash
set -euo pipefail

cc=${CC:-clang}
flags=(
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

sources=(
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
)

for source in "${sources[@]}"; do
    printf 'WIN32-SURFACE %s\n' "$source"
    "$cc" "${flags[@]}" "$source"
done
