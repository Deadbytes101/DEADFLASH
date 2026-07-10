#include "deadflash/attest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int make_file(const char *path, size_t length, unsigned seed) {
    FILE *file = fopen(path, "wb");
    size_t i;
    if (file == NULL) return 0;
    for (i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)((i * seed + 11u) & 255u);
        if (fwrite(&byte, 1u, 1u, file) != 1u) {
            fclose(file);
            return 0;
        }
    }
    return fclose(file) == 0;
}

int main(void) {
    const char *source = "attest-source.bin";
    const char *target = "attest-target.bin";
    df_write_options options;
    df_plan_attestation first;
    df_plan_attestation second;
    df_write_result result;
    df_target_info target_info;
    df_error error;
    char wrong_seal[65];

    if (!make_file(source, 1024u * 1024u + 7u, 17u) ||
        !make_file(target, 1024u * 1024u + 7u, 3u))
        return 1;

    memset(&options, 0, sizeof(options));
    options.buffer_size = 1024u * 1024u;
    options.write_retries = 4u;
    options.verify_mode = DF_VERIFY_FULL;
    options.sample_count = 64u;
    options.truncate_regular_file = true;

    if (df_attest_plan(source, target, &options,
                       &first, &error) != DF_OK)
        return 2;
    if (df_attest_plan(source, target, &options,
                       &second, &error) != DF_OK)
        return 3;
    if (strcmp(first.plan_hex, second.plan_hex) != 0) return 4;

    options.verify_mode = DF_VERIFY_SAMPLE;
    if (df_attest_plan(source, target, &options,
                       &second, &error) != DF_OK)
        return 5;
    if (strcmp(first.plan_hex, second.plan_hex) == 0) return 6;

    options.verify_mode = DF_VERIFY_FULL;
    memset(wrong_seal, '0', 64u);
    wrong_seal[64] = '\0';
    if (df_write_image_attested(source, target, &options, wrong_seal,
                                &target_info, &result, &error) !=
        DF_ERR_CONFIRMATION)
        return 7;

    if (df_write_image_attested(source, target, &options, first.plan_hex,
                                &target_info, &result, &error) != DF_OK)
        return 8;
    if (strcmp(result.final_state, "success_verified") != 0) return 9;
    if (!df_constant_time_equal(result.source_sha256,
                                first.source_sha256, 32u))
        return 10;

    (void)remove(source);
    (void)remove(target);
    return 0;
}
