#include "deadflash/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_PATH "deadflash-test-source.bin"
#define TARGET_PATH "deadflash-test-target.bin"
#define REPORT_SIZE (8u * 1024u * 1024u + 123u)
#define PHASE_COUNT 6u

typedef struct progress_record {
    bool seen[PHASE_COUNT];
    uint64_t last_completed[PHASE_COUNT];
    uint64_t last_total[PHASE_COUNT];
    unsigned first_order[PHASE_COUNT];
    unsigned next_order;
    bool failed;
} progress_record;

static void record_progress(void *context, df_progress_phase phase,
                            uint64_t completed, uint64_t total) {
    progress_record *record = (progress_record *)context;
    const unsigned index = (unsigned)phase;
    if (record == NULL || index >= PHASE_COUNT) return;
    if (total != 0u && completed > total) record->failed = true;
    if (record->seen[index] && completed < record->last_completed[index]) {
        record->failed = true;
    }
    if (!record->seen[index]) {
        record->seen[index] = true;
        record->first_order[index] = record->next_order++;
    }
    record->last_completed[index] = completed;
    record->last_total[index] = total;
}

static int create_source(void) {
    FILE *file = fopen(SOURCE_PATH, "wb");
    uint8_t buffer[65536];
    size_t total = 0u;
    size_t i;
    if (file == NULL) return 1;
    for (i = 0u; i < sizeof(buffer); ++i) {
        buffer[i] = (uint8_t)((i * 29u + 113u) & 0xffu);
    }
    while (total < REPORT_SIZE) {
        size_t n = REPORT_SIZE - total < sizeof(buffer) ?
                   REPORT_SIZE - total : sizeof(buffer);
        if (fwrite(buffer, 1u, n, file) != n) {
            fclose(file);
            return 1;
        }
        total += n;
    }
    return fclose(file) != 0;
}

static int validate_progress(const progress_record *record) {
    unsigned i;
    if (record->failed || record->next_order != PHASE_COUNT) return 1;
    for (i = 0u; i < PHASE_COUNT; ++i) {
        if (!record->seen[i] || record->first_order[i] != i) return 1;
        if (record->last_total[i] != 0u &&
            record->last_completed[i] != record->last_total[i]) return 1;
    }
    return 0;
}

int main(void) {
    df_write_options options;
    df_write_result result;
    df_target_info info;
    df_error error;
    progress_record progress;
    FILE *target;
    uint8_t preflight_hash[32];
    uint64_t preflight_size = 0u;
    int rc = 1;

    remove(SOURCE_PATH);
    remove(TARGET_PATH);
    if (create_source() != 0) {
        fprintf(stderr, "could not create source\n");
        goto out;
    }

    if (df_hash_source_path(SOURCE_PATH, 1024u * 1024u,
                            &preflight_size, preflight_hash,
                            NULL, NULL, &error) != DF_OK ||
        preflight_size != REPORT_SIZE) {
        fprintf(stderr, "source preflight failed: %s\n", error.message);
        goto out;
    }

    memset(&progress, 0, sizeof(progress));
    memset(&options, 0, sizeof(options));
    options.buffer_size = 1024u * 1024u;
    options.write_retries = 2u;
    options.verify_mode = DF_VERIFY_FULL;
    options.truncate_regular_file = true;
    options.progress_callback = record_progress;
    options.progress_context = &progress;

    if (df_write_image(SOURCE_PATH, TARGET_PATH, &options,
                       &info, &result, &error) != DF_OK) {
        fprintf(stderr, "write failed: %s\n", error.message);
        goto out;
    }
    if (result.bytes_written != REPORT_SIZE ||
        result.bytes_verified != REPORT_SIZE ||
        strcmp(result.final_state, "success_verified") != 0 ||
        !df_constant_time_equal(preflight_hash, result.source_sha256, 32u)) {
        fprintf(stderr, "unexpected result state\n");
        goto out;
    }
    if (validate_progress(&progress) != 0) {
        fprintf(stderr, "progress callback contract failed\n");
        goto out;
    }

    if (df_verify_image(SOURCE_PATH, TARGET_PATH, DF_VERIFY_SAMPLE,
                        8u, 1024u * 1024u, &result, &error) != DF_OK) {
        fprintf(stderr, "sample verify failed: %s\n", error.message);
        goto out;
    }

    target = fopen(TARGET_PATH, "r+b");
    if (target == NULL) goto out;
    if (fseek(target, 4096L, SEEK_SET) != 0 ||
        fputc(0x42, target) == EOF || fclose(target) != 0) goto out;
    if (df_verify_image(SOURCE_PATH, TARGET_PATH, DF_VERIFY_FULL,
                        0u, 1024u * 1024u,
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
