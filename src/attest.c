#include "deadflash/attest.h"
#include "deadflash/sha256.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode_plan_hex(const char *text, uint8_t output[32]) {
    size_t i;
    if (text == NULL || strlen(text) != 64u) return false;
    for (i = 0; i < 32u; ++i) {
        int high = hex_nibble(text[i * 2u]);
        int low = hex_nibble(text[i * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void attest_file_reset(df_file *file) {
    memset(file, 0, sizeof(*file));
    file->handle = DF_INVALID_HANDLE;
    file->alignment = 512u;
}

df_status df_attest_plan(const char *source_path, const char *target_path,
                         const df_write_options *options_in,
                         df_plan_attestation *attestation, df_error *error) {
    df_write_options options;
    df_file source;
    char source_hex[65];
    char canonical[1024];
    int written;
    df_status status;

    df_error_clear(error);
    if (source_path == NULL || target_path == NULL || attestation == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "attestation requires source, target, and output");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(&options, 0, sizeof(options));
    if (options_in != NULL) options = *options_in;
    if (options.buffer_size == 0u) options.buffer_size = DF_DEFAULT_BUFFER_SIZE;
    if (options.write_retries == 0u) options.write_retries = 4u;
    if (options.sample_count == 0u) options.sample_count = 64u;

    memset(attestation, 0, sizeof(*attestation));
    attest_file_reset(&source);
    status = df_open_source(source_path, &source, error);
    if (status != DF_OK) return status;
    if (source.size_bytes == 0u) {
        df_error_set(error, DF_ERR_TOO_SMALL, 0,
                     "cannot attest an empty source image");
        status = DF_ERR_TOO_SMALL;
        goto out;
    }
    attestation->source_size = source.size_bytes;
    status = df_hash_file_region(&source, source.size_bytes, options.buffer_size,
                                 attestation->source_sha256, error);
    if (status != DF_OK) goto out;
    status = df_inspect_target(target_path, &attestation->target, error);
    if (status != DF_OK) goto out;

    df_hex_encode(attestation->source_sha256, 32u, source_hex);
    written = snprintf(canonical, sizeof(canonical),
                       "deadflash.plan.v1\n"
                       "source_size=%" PRIu64 "\n"
                       "source_sha256=%s\n"
                       "target_token=%s\n"
                       "target_kind=%u\n"
                       "target_size=%" PRIu64 "\n"
                       "logical_sector=%u\n"
                       "physical_sector=%u\n"
                       "verify_mode=%u\n"
                       "sample_count=%u\n"
                       "buffer_size=%zu\n"
                       "write_retries=%u\n"
                       "direct_io=%u\n"
                       "truncate_regular_file=%u\n",
                       attestation->source_size, source_hex,
                       attestation->target.token,
                       (unsigned)attestation->target.kind,
                       attestation->target.size_bytes,
                       attestation->target.logical_sector_size,
                       attestation->target.physical_sector_size,
                       (unsigned)options.verify_mode,
                       options.sample_count,
                       options.buffer_size,
                       options.write_retries,
                       options.direct_io ? 1u : 0u,
                       options.truncate_regular_file ? 1u : 0u);
    if (written < 0 || (size_t)written >= sizeof(canonical)) {
        df_error_set(error, DF_ERR_INTERNAL, 0,
                     "canonical plan exceeded fixed buffer");
        status = DF_ERR_INTERNAL;
        goto out;
    }
    df_sha256_buffer(canonical, (size_t)written,
                     attestation->plan_sha256);
    df_hex_encode(attestation->plan_sha256, 32u, attestation->plan_hex);
    status = DF_OK;
out:
    df_close_file(&source);
    return status;
}

df_status df_write_image_attested(const char *source_path,
                                  const char *target_path,
                                  const df_write_options *options_in,
                                  const char *expected_plan_hex,
                                  df_target_info *target_info,
                                  df_write_result *result,
                                  df_error *error) {
    df_write_options options;
    df_plan_attestation attestation;
    uint8_t expected[32];
    df_status status;

    df_error_clear(error);
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
        (void)snprintf(result->final_state, sizeof(result->final_state),
                       "failed_before_write");
    }
    if (target_info != NULL) memset(target_info, 0, sizeof(*target_info));
    if (options_in == NULL || expected_plan_hex == NULL || result == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "attested write requires options, plan seal, and result");
        return DF_ERR_INVALID_ARGUMENT;
    }
    if (!decode_plan_hex(expected_plan_hex, expected)) {
        df_error_set(error, DF_ERR_CONFIRMATION, 0,
                     "plan seal must be exactly 64 hexadecimal characters");
        return DF_ERR_CONFIRMATION;
    }
    options = *options_in;
    status = df_attest_plan(source_path, target_path, &options,
                            &attestation, error);
    if (status != DF_OK) return status;
    if (!df_constant_time_equal(expected, attestation.plan_sha256, 32u)) {
        df_error_set(error, DF_ERR_CONFIRMATION, 0,
                     "operation plan changed; generate a new plan seal");
        return DF_ERR_CONFIRMATION;
    }
    status = df_write_image(source_path, target_path, &options,
                            target_info, result, error);
    if (status == DF_OK &&
        !df_constant_time_equal(result->source_sha256,
                                attestation.source_sha256, 32u)) {
        (void)snprintf(result->final_state, sizeof(result->final_state),
                       "plan_breach_partial_media");
        df_error_set(error, DF_ERR_IDENTITY_CHANGED, 0,
                     "written source hash no longer matches the authorized plan seal");
        return DF_ERR_IDENTITY_CHANGED;
    }
    return status;
}
