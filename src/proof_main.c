#include "deadflash/attest.h"
#include "deadflash/proof.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out) {
    fprintf(out,
            "DEADFLASH PROOF %s\n"
            "PLAN-SEAL + CHUNK PROOF + EXACT MISMATCH LOCATION\n\n"
            "USAGE\n"
            "  deadflash-proof seal IMAGE TARGET [OPTIONS]\n"
            "  deadflash-proof write IMAGE TARGET --seal HEX [OPTIONS]\n"
            "  deadflash-proof manifest IMAGE PROOF [--chunk SIZE]\n"
            "  deadflash-proof verify PROOF IMAGE TARGET\n\n"
            "OPTIONS\n"
            "  --verify none|sample|full   Default: full\n"
            "  --samples N                 Default: 64\n"
            "  --buffer SIZE               Default: 32MiB\n"
            "  --direct                    Direct I/O for physical targets\n"
            "  --allow-device              Permit physical-device writes\n"
            "  --confirm TOKEN             Current target confirmation token\n"
            "  --force-mounted             Dangerous override\n"
            "  --force-system-disk         Extremely dangerous override\n"
            "  --proof PATH                Create and verify proof after write\n"
            "  --chunk SIZE                Proof chunk size; default: 4MiB\n",
            DF_VERSION_STRING);
}

static int fail(const df_error *error) {
    if (error != NULL) {
        fprintf(stderr, "ERROR[%s]", df_status_name(error->code));
        if (error->os_code != 0)
            fprintf(stderr, "(os=%d)", error->os_code);
        if (error->message[0] != '\0')
            fprintf(stderr, ": %s", error->message);
        fputc('\n', stderr);
    }
    return 1;
}

static const char *next_value(int argc, char **argv, int *index,
                              const char *name) {
    const char *arg = argv[*index];
    size_t length = strlen(name);
    if (strncmp(arg, name, length) == 0 && arg[length] == '=')
        return arg + length + 1u;
    if (strcmp(arg, name) == 0 && *index + 1 < argc)
        return argv[++(*index)];
    return NULL;
}

static df_verify_mode parse_verify(const char *text, bool *ok) {
    *ok = true;
    if (strcmp(text, "none") == 0) return DF_VERIFY_NONE;
    if (strcmp(text, "sample") == 0) return DF_VERIFY_SAMPLE;
    if (strcmp(text, "full") == 0) return DF_VERIFY_FULL;
    *ok = false;
    return DF_VERIFY_NONE;
}

static void default_options(df_write_options *options) {
    memset(options, 0, sizeof(*options));
    options->buffer_size = DF_DEFAULT_BUFFER_SIZE;
    options->write_retries = 4u;
    options->verify_mode = DF_VERIFY_FULL;
    options->sample_count = 64u;
    options->truncate_regular_file = true;
}

static int parse_write_options(int argc, char **argv, int first,
                               df_write_options *options,
                               const char **seal,
                               const char **proof_path,
                               size_t *proof_chunk) {
    int i;
    for (i = first; i < argc; ++i) {
        const char *value;
        if ((value = next_value(argc, argv, &i, "--verify")) != NULL) {
            bool ok;
            options->verify_mode = parse_verify(value, &ok);
            if (!ok) return 0;
        } else if ((value = next_value(argc, argv, &i,
                                      "--samples")) != NULL) {
            uint64_t parsed;
            if (!df_parse_u64(value, &parsed) || parsed > UINT32_MAX)
                return 0;
            options->sample_count = (unsigned)parsed;
        } else if ((value = next_value(argc, argv, &i,
                                      "--buffer")) != NULL) {
            uint64_t parsed;
            if (!df_parse_size(value, &parsed) || parsed == 0u ||
                parsed > SIZE_MAX)
                return 0;
            options->buffer_size = (size_t)parsed;
        } else if ((value = next_value(argc, argv, &i,
                                      "--confirm")) != NULL) {
            options->confirmation_token = value;
        } else if ((value = next_value(argc, argv, &i,
                                      "--seal")) != NULL) {
            if (seal == NULL) return 0;
            *seal = value;
        } else if ((value = next_value(argc, argv, &i,
                                      "--proof")) != NULL) {
            if (proof_path == NULL) return 0;
            *proof_path = value;
        } else if ((value = next_value(argc, argv, &i,
                                      "--chunk")) != NULL) {
            uint64_t parsed;
            if (proof_chunk == NULL || !df_parse_size(value, &parsed) ||
                parsed == 0u || parsed > SIZE_MAX)
                return 0;
            *proof_chunk = (size_t)parsed;
        } else if (strcmp(argv[i], "--direct") == 0) {
            options->direct_io = true;
        } else if (strcmp(argv[i], "--allow-device") == 0) {
            options->allow_device = true;
        } else if (strcmp(argv[i], "--force-mounted") == 0) {
            options->force_mounted = true;
        } else if (strcmp(argv[i], "--force-system-disk") == 0) {
            options->force_system_disk = true;
        } else {
            return 0;
        }
    }
    return 1;
}

static int command_seal(int argc, char **argv) {
    df_write_options options;
    df_plan_attestation attestation;
    df_error error;
    char source_hex[65];
    if (argc < 4) return 2;
    default_options(&options);
    if (!parse_write_options(argc, argv, 4, &options,
                             NULL, NULL, NULL))
        return 2;
    if (df_attest_plan(argv[2], argv[3], &options,
                       &attestation, &error) != DF_OK)
        return fail(&error);
    df_hex_encode(attestation.source_sha256, 32u, source_hex);
    printf("SOURCE_SIZE_BYTES  : %" PRIu64 "\n",
           attestation.source_size);
    printf("SOURCE_SHA256      : %s\n", source_hex);
    printf("TARGET_TOKEN       : %s\n", attestation.target.token);
    printf("TARGET_SIZE_BYTES  : %" PRIu64 "\n",
           attestation.target.size_bytes);
    printf("VERIFY_MODE        : %u\n", (unsigned)options.verify_mode);
    printf("BUFFER_SIZE        : %zu\n", options.buffer_size);
    printf("PLAN_SEAL          : %s\n", attestation.plan_hex);
    return 0;
}

static int command_manifest(int argc, char **argv) {
    size_t chunk_size = DF_PROOF_DEFAULT_CHUNK_SIZE;
    df_proof_summary summary;
    df_error error;
    char source_hex[65];
    char root_hex[65];
    int i;
    if (argc < 4) return 2;
    for (i = 4; i < argc; ++i) {
        const char *value = next_value(argc, argv, &i, "--chunk");
        uint64_t parsed;
        if (value == NULL || !df_parse_size(value, &parsed) ||
            parsed == 0u || parsed > SIZE_MAX)
            return 2;
        chunk_size = (size_t)parsed;
    }
    if (df_proof_create(argv[2], argv[3], chunk_size,
                        &summary, &error) != DF_OK)
        return fail(&error);
    df_hex_encode(summary.source_sha256, 32u, source_hex);
    df_hex_encode(summary.merkle_root, 32u, root_hex);
    printf("STATE              : proof_created\n");
    printf("SOURCE_SIZE_BYTES  : %" PRIu64 "\n", summary.source_size);
    printf("CHUNK_SIZE         : %" PRIu64 "\n", summary.chunk_size);
    printf("CHUNK_COUNT        : %" PRIu64 "\n", summary.chunk_count);
    printf("SOURCE_SHA256      : %s\n", source_hex);
    printf("MERKLE_ROOT        : %s\n", root_hex);
    return 0;
}

static int command_verify(int argc, char **argv) {
    df_proof_result result;
    df_error error;
    df_status status;
    if (argc != 5) return 2;
    status = df_proof_verify(argv[2], argv[3], argv[4],
                             &result, &error);
    printf("STATE              : %s\n", result.final_state);
    printf("BYTES_VERIFIED     : %" PRIu64 "\n",
           result.bytes_verified);
    printf("CHUNKS_VERIFIED    : %" PRIu64 "\n",
           result.chunks_verified);
    if (result.first_bad_offset != UINT64_MAX)
        printf("FIRST_BAD_OFFSET   : %" PRIu64 "\n",
               result.first_bad_offset);
    return status == DF_OK ? 0 : fail(&error);
}

static int command_write(int argc, char **argv) {
    df_write_options options;
    df_write_result write_result;
    df_target_info target;
    df_error error;
    const char *seal = NULL;
    const char *proof_path = NULL;
    size_t proof_chunk = DF_PROOF_DEFAULT_CHUNK_SIZE;
    df_status status;
    if (argc < 4) return 2;
    default_options(&options);
    if (!parse_write_options(argc, argv, 4, &options,
                             &seal, &proof_path, &proof_chunk) ||
        seal == NULL)
        return 2;
    status = df_write_image_attested(argv[2], argv[3], &options, seal,
                                     &target, &write_result, &error);
    printf("STATE              : %s\n", write_result.final_state);
    printf("BYTES_WRITTEN      : %" PRIu64 "\n",
           write_result.bytes_written);
    printf("BYTES_VERIFIED     : %" PRIu64 "\n",
           write_result.bytes_verified);
    printf("TOTAL_MS           : %.3f\n", write_result.total_ms);
    if (status != DF_OK) return fail(&error);
    if (proof_path != NULL) {
        df_proof_summary summary;
        df_proof_result proof_result;
        status = df_proof_create(argv[2], proof_path, proof_chunk,
                                 &summary, &error);
        if (status != DF_OK) return fail(&error);
        status = df_proof_verify(proof_path, argv[2], argv[3],
                                 &proof_result, &error);
        printf("PROOF_STATE        : %s\n", proof_result.final_state);
        printf("PROOF_BYTES        : %" PRIu64 "\n",
               proof_result.bytes_verified);
        if (status != DF_OK) return fail(&error);
    }
    return 0;
}

int main(int argc, char **argv) {
    int result;
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (strcmp(argv[1], "seal") == 0)
        result = command_seal(argc, argv);
    else if (strcmp(argv[1], "write") == 0)
        result = command_write(argc, argv);
    else if (strcmp(argv[1], "manifest") == 0)
        result = command_manifest(argc, argv);
    else if (strcmp(argv[1], "verify") == 0)
        result = command_verify(argc, argv);
    else if (strcmp(argv[1], "version") == 0) {
        printf("DEADFLASH PROOF %s\n", DF_VERSION_STRING);
        return 0;
    } else {
        usage(stderr);
        return 2;
    }
    if (result == 2) usage(stderr);
    return result;
}
