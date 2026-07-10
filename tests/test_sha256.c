#include "deadflash/sha256.h"

#include <stdio.h>
#include <string.h>

static int check(const char *message, const char *expected) {
    uint8_t digest[32];
    char hex[65];
    df_sha256_buffer(message, strlen(message), digest);
    df_hex_encode(digest, 32, hex);
    if (strcmp(hex, expected) != 0) {
        fprintf(stderr, "SHA-256 mismatch\nexpected: %s\nactual:   %s\n", expected, hex);
        return 1;
    }
    return 0;
}

int main(void) {
    df_sha256_ctx ctx;
    uint8_t digest[32];
    char hex[65];
    const char *message = "The quick brown fox jumps over the lazy dog";

    if (check("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") != 0) return 1;
    if (check("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0) return 1;
    if (check(message, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592") != 0) return 1;

    df_sha256_init(&ctx);
    df_sha256_update(&ctx, message, 10);
    df_sha256_update(&ctx, message + 10, strlen(message) - 10);
    df_sha256_final(&ctx, digest);
    df_hex_encode(digest, 32, hex);
    if (strcmp(hex, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592") != 0) {
        fprintf(stderr, "incremental SHA-256 mismatch\n");
        return 1;
    }
    return 0;
}
