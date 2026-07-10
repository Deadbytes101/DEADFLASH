#include "deadflash/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_PATH "deadflash-test-source.bin"
#define TARGET_PATH "deadflash-test-target.bin"
#define REPORT_SIZE (8u * 1024u * 1024u + 123u)

static int create_source(void) {
    FILE *file = fopen(SOURCE_PATH, "wb");
    uint8_t buffer[65536];
    size_t total = 0;
    size_t i;
    if (file == NULL) return 1;
    for (i = 0; i < sizeof(buffer); ++i) buffer[i] = (uint8_t)((i * 29u + 113u) & 0xffu);
    while (total < REPORT_SIZE) {
        size_t n = REPORT_SIZE - total < sizeof(buffer) ? REPORT_SIZE - total : sizeof(buffer);
        if (fwrite(buffer, 1, n, file) != n) { fclose(file); return 1; }
        total += n;
    }
    return fclose(file) != 0;
}

int main(void) {
    df_write_options options;
    df_write_result result;
    df_target_info info;
    df_error error;
    FILE *target;
    int rc = 1;

    remove(SOURCE_PATH);
    remove(TARGET_PATH);
    if (create_source() != 0) {
        fprintf(stderr, "could not create source\n");
        goto out;
    }
    memset(&options, 0, sizeof(options));
    options.buffer_size = 1024u * 1024u;
    options.write_retries = 2;
    options.verify_mode = DF_VERIFY_FULL;
    options.truncate_regular_file = true;

    if (df_write_image(SOURCE_PATH, TARGET_PATH, &options, &info, &result, &error) != DF_OK) {
        fprintf(stderr, "write failed: %s\n", error.message);
        goto out;
    }
    if (result.bytes_written != REPORT_SIZE || result.bytes_verified != REPORT_SIZE ||
        strcmp(result.final_state, "success_verified") != 0) {
        fprintf(stderr, "unexpected result state\n");
        goto out;
    }
    if (df_verify_image(SOURCE_PATH, TARGET_PATH, DF_VERIFY_SAMPLE, 8, 1024u * 1024u,
                        &result, &error) != DF_OK) {
        fprintf(stderr, "sample verify failed: %s\n", error.message);
        goto out;
    }

    target = fopen(TARGET_PATH, "r+b");
    if (target == NULL) goto out;
    if (fseek(target, 4096, SEEK_SET) != 0 || fputc(0x42, target) == EOF || fclose(target) != 0) goto out;
    if (df_verify_image(SOURCE_PATH, TARGET_PATH, DF_VERIFY_FULL, 0, 1024u * 1024u,
                        &result, &error) != DF_ERR_VERIFY_MISMATCH) {
        fprintf(stderr, "corruption was not detected\n");
        goto out;
    }
    rc = 0;
out:
    remove(SOURCE_PATH);
    remove(TARGET_PATH);
    return rc;
}
