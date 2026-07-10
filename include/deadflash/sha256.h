#ifndef DEADFLASH_SHA256_H
#define DEADFLASH_SHA256_H

#include "deadflash/common.h"

typedef struct df_sha256_ctx {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_len;
} df_sha256_ctx;

void df_sha256_init(df_sha256_ctx *ctx);
void df_sha256_update(df_sha256_ctx *ctx, const void *data, size_t length);
void df_sha256_final(df_sha256_ctx *ctx, uint8_t digest[32]);
void df_sha256_buffer(const void *data, size_t length, uint8_t digest[32]);

#endif
