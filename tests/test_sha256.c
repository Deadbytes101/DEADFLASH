#include "deadflash/sha256.h"

#include <stdio.h>
#include <string.h>

typedef struct sha256_vector {
    size_t length;
    const char *expected;
} sha256_vector;

static int digest_matches(const uint8_t digest[32], const char *expected,
                          const char *mode, size_t length) {
    char hex[65];

    df_hex_encode(digest, 32u, hex);
    if (strcmp(hex, expected) != 0) {
        fprintf(stderr,
                "SHA-256 mismatch (%s, length=%zu)\n"
                "expected: %s\n"
                "actual:   %s\n",
                mode, length, expected, hex);
        return 1;
    }
    return 0;
}

static int check_one_shot(const uint8_t *data, size_t length,
                          const char *expected) {
    uint8_t digest[32];

    df_sha256_buffer(data, length, digest);
    return digest_matches(digest, expected, "one-shot", length);
}

static int check_fragmented(const uint8_t *data, size_t length,
                            const char *expected) {
    df_sha256_ctx ctx;
    uint8_t digest[32];
    size_t offset = 0u;
    size_t step = 1u;

    df_sha256_init(&ctx);
    while (offset < length) {
        size_t chunk = ((step * 5u) % 17u) + 1u;
        const size_t remaining = length - offset;

        if (chunk > remaining) chunk = remaining;
        df_sha256_update(&ctx, data + offset, chunk);
        offset += chunk;
        ++step;
    }
    df_sha256_final(&ctx, digest);
    return digest_matches(digest, expected, "fragmented", length);
}

static int check_vector(const uint8_t *data, size_t length,
                        const char *expected) {
    if (check_one_shot(data, length, expected) != 0) return 1;
    return check_fragmented(data, length, expected);
}

int main(void) {
    static const sha256_vector pattern_vectors[] = {
        {0u, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {1u, "e7cf46a078fed4fafd0b5e3aff144802b853f8ae459a4f0c14add3314b7cc3a6"},
        {55u, "2900465fcb533e05a158fd2b3be0e5e3b03740d83060aa3580e0d98a96bf2384"},
        {56u, "31454ff48ef36af2f08fd511bdc37d9d5855ac23e992e5ff5445cb6b7674a674"},
        {63u, "5f6401b96532c36de4e65beec0409b69b1d181864c8009b7a04f43e5d56350d1"},
        {64u, "94eb5de4943613fd048dc93393ab06877405faa39c11f53e9386083339833e7e"},
        {65u, "fc518669b6eb4b4dd91827ecacef86689c725bd5bab888fd3b26dbb196eec954"},
        {127u, "0fe729ff19257bd6fec853acc2ea355f6b34b58e6c0f684c3e188fcdfcd9baae"},
        {128u, "0aedd4856f8eba0963627336ad5144a9a7dbe12498e6066f0165fc97d8ddee4c"},
        {129u, "4f1757ae4bffbae86d775b831765b75af154d52f7deaa46dd378051a2d3ad57f"}
    };
    static const uint8_t abc[] = {'a', 'b', 'c'};
    static const uint8_t quick_brown_fox[] =
        "The quick brown fox jumps over the lazy dog";
    uint8_t pattern[129];
    size_t i;

    for (i = 0u; i < sizeof(pattern); ++i) {
        pattern[i] = (uint8_t)((i * 37u + 11u) & 0xffu);
    }

    if (check_vector(abc, sizeof(abc),
                     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0) {
        return 1;
    }
    if (check_vector(quick_brown_fox, sizeof(quick_brown_fox) - 1u,
                     "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592") != 0) {
        return 1;
    }

    for (i = 0u; i < sizeof(pattern_vectors) / sizeof(pattern_vectors[0]); ++i) {
        if (check_vector(pattern, pattern_vectors[i].length,
                         pattern_vectors[i].expected) != 0) {
            return 1;
        }
    }

    return 0;
}
