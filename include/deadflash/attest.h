#ifndef DEADFLASH_ATTEST_H
#define DEADFLASH_ATTEST_H

#include "deadflash/io.h"

typedef struct df_plan_attestation {
    uint64_t source_size;
    uint8_t source_sha256[32];
    df_target_info target;
    uint8_t plan_sha256[32];
    char plan_hex[DF_SHA256_HEX_CHARS + 1];
} df_plan_attestation;

df_status df_attest_plan(const char *source_path, const char *target_path,
                         const df_write_options *options,
                         df_plan_attestation *attestation, df_error *error);
df_status df_write_image_attested(const char *source_path,
                                  const char *target_path,
                                  const df_write_options *options,
                                  const char *expected_plan_hex,
                                  df_target_info *target_info,
                                  df_write_result *result,
                                  df_error *error);

#endif
