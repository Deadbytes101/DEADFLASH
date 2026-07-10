#include "deadflash/fat32.h"

#include <stdio.h>
#include <string.h>

#define IMAGE_PATH "deadflash-test-fat32.img"

int main(void) {
    df_fat32_options options;
    df_fat32_layout written;
    df_fat32_layout verified;
    df_target_info info;
    df_error error;
    char label[12];
    int rc = 1;

    remove(IMAGE_PATH);
    memset(&options, 0, sizeof(options));
    options.create_mbr = true;
    options.partition_start_lba = 2048;
    options.regular_file_size = 128ULL * 1024ULL * 1024ULL;
    memcpy(options.label, "BYTECORE", 9);

    if (df_format_fat32(IMAGE_PATH, &options, &info, &written, &error) != DF_OK) {
        fprintf(stderr, "format failed: %s\n", error.message);
        goto out;
    }
    if (df_verify_fat32(IMAGE_PATH, &verified, label, &error) != DF_OK) {
        fprintf(stderr, "verify failed: %s\n", error.message);
        goto out;
    }
    if (written.partition_start_lba != 2048 || verified.partition_start_lba != 2048 ||
        written.partition_sectors != verified.partition_sectors ||
        written.sectors_per_fat != verified.sectors_per_fat ||
        strncmp(label, "BYTECORE", 8) != 0) {
        fprintf(stderr, "FAT32 layout mismatch\n");
        goto out;
    }
    rc = 0;
out:
    remove(IMAGE_PATH);
    return rc;
}
