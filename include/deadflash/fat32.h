#ifndef DEADFLASH_FAT32_H
#define DEADFLASH_FAT32_H

#include "deadflash/io.h"

typedef struct df_fat32_options {
    char label[12];
    uint32_t partition_start_lba;
    bool create_mbr;
    uint64_t regular_file_size;
    bool allow_device;
    bool force_mounted;
    bool force_system_disk;
    const char *confirmation_token;
} df_fat32_options;

typedef struct df_fat32_layout {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t sectors_per_fat;
    uint32_t partition_start_lba;
    uint32_t partition_sectors;
    uint32_t data_clusters;
    uint32_t root_cluster;
} df_fat32_layout;

df_status df_fat32_compute_layout(uint64_t target_size, const df_fat32_options *options,
                                  df_fat32_layout *layout, df_error *error);
df_status df_format_fat32(const char *target_path, const df_fat32_options *options,
                          df_target_info *target_info, df_fat32_layout *layout,
                          df_error *error);
df_status df_verify_fat32(const char *target_path, df_fat32_layout *layout,
                          char label[12], df_error *error);
void df_fat32_normalize_label(const char *input, char output[12]);

#endif
