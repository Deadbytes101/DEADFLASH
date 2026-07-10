#include "deadflash/fat32.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define DF_FAT32_MIN_CLUSTERS 65525u
#define DF_FAT32_MAX_CLUSTERS 0x0ffffff5u
#define DF_FAT32_RESERVED_SECTORS 32u
#define DF_FAT32_FAT_COUNT 2u
#define DF_FAT32_ROOT_CLUSTER 2u
#define DF_FAT32_FSINFO_SECTOR 1u
#define DF_FAT32_BACKUP_BOOT_SECTOR 6u

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void df_fat32_normalize_label(const char *input, char output[12]) {
    size_t i;
    memset(output, ' ', 11);
    output[11] = '\0';
    if (input == NULL || *input == '\0') input = "DEADFLASH";
    for (i = 0; i < 11 && input[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)input[i];
        if (c < 0x20 || strchr("\"*+,./:;<=>?[\\]|", c) != NULL) output[i] = '_';
        else output[i] = (char)toupper(c);
    }
}

static uint32_t choose_sectors_per_cluster(uint64_t partition_bytes) {
    if (partition_bytes <= 260ULL * 1024ULL * 1024ULL) return 1;
    if (partition_bytes <= 8ULL * 1024ULL * 1024ULL * 1024ULL) return 8;
    if (partition_bytes <= 16ULL * 1024ULL * 1024ULL * 1024ULL) return 16;
    if (partition_bytes <= 32ULL * 1024ULL * 1024ULL * 1024ULL) return 32;
    return 64;
}

df_status df_fat32_compute_layout(uint64_t target_size, const df_fat32_options *options,
                                  df_fat32_layout *layout, df_error *error) {
    uint64_t total_sectors64;
    uint32_t total_sectors;
    uint32_t partition_start;
    uint32_t sectors_per_cluster;
    uint32_t sectors_per_fat = 1;
    uint32_t previous = 0;
    uint32_t clusters = 0;
    unsigned iteration;
    if (layout == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "FAT32 layout output is required");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(layout, 0, sizeof(*layout));
    partition_start = options != NULL && options->partition_start_lba != 0 ?
                      options->partition_start_lba : 2048u;
    if (target_size < (uint64_t)(partition_start + 131072u) * 512ULL) {
        df_error_set(error, DF_ERR_TOO_SMALL, 0,
                     "FAT32 target must be at least 65 MiB after partition alignment");
        return DF_ERR_TOO_SMALL;
    }
    total_sectors64 = target_size / 512ULL;
    if (total_sectors64 <= partition_start || total_sectors64 - partition_start > UINT32_MAX) {
        df_error_set(error, DF_ERR_TOO_LARGE, 0,
                     "MBR FAT32 formatter supports at most 2 TiB with 512-byte sectors");
        return DF_ERR_TOO_LARGE;
    }
    total_sectors = (uint32_t)(total_sectors64 - partition_start);
    sectors_per_cluster = choose_sectors_per_cluster((uint64_t)total_sectors * 512ULL);

    for (iteration = 0; iteration < 64; ++iteration) {
        uint32_t data_sectors;
        uint64_t fat_bytes;
        if (total_sectors <= DF_FAT32_RESERVED_SECTORS + DF_FAT32_FAT_COUNT * sectors_per_fat) {
            df_error_set(error, DF_ERR_TOO_SMALL, 0, "target is too small for FAT32 metadata");
            return DF_ERR_TOO_SMALL;
        }
        data_sectors = total_sectors - DF_FAT32_RESERVED_SECTORS -
                       DF_FAT32_FAT_COUNT * sectors_per_fat;
        clusters = data_sectors / sectors_per_cluster;
        fat_bytes = ((uint64_t)clusters + 2ULL) * 4ULL;
        previous = sectors_per_fat;
        sectors_per_fat = (uint32_t)((fat_bytes + 511ULL) / 512ULL);
        if (sectors_per_fat == previous) break;
    }
    if (clusters < DF_FAT32_MIN_CLUSTERS) {
        df_error_set(error, DF_ERR_TOO_SMALL, 0,
                     "computed cluster count %u is below FAT32 minimum", clusters);
        return DF_ERR_TOO_SMALL;
    }
    if (clusters >= DF_FAT32_MAX_CLUSTERS) {
        df_error_set(error, DF_ERR_TOO_LARGE, 0,
                     "computed cluster count %u exceeds FAT32 limit", clusters);
        return DF_ERR_TOO_LARGE;
    }
    layout->bytes_per_sector = 512;
    layout->sectors_per_cluster = sectors_per_cluster;
    layout->reserved_sectors = DF_FAT32_RESERVED_SECTORS;
    layout->fat_count = DF_FAT32_FAT_COUNT;
    layout->sectors_per_fat = sectors_per_fat;
    layout->partition_start_lba = partition_start;
    layout->partition_sectors = total_sectors;
    layout->data_clusters = clusters;
    layout->root_cluster = DF_FAT32_ROOT_CLUSTER;
    return DF_OK;
}

static void build_mbr(uint8_t sector[512], const df_fat32_layout *layout) {
    uint8_t *entry;
    memset(sector, 0, 512);
    entry = sector + 446;
    entry[0] = 0x00;
    entry[1] = 0xfe; entry[2] = 0xff; entry[3] = 0xff;
    entry[4] = 0x0c;
    entry[5] = 0xfe; entry[6] = 0xff; entry[7] = 0xff;
    put_le32(entry + 8, layout->partition_start_lba);
    put_le32(entry + 12, layout->partition_sectors);
    sector[510] = 0x55;
    sector[511] = 0xaa;
}

static void build_boot_sector(uint8_t sector[512], const df_fat32_layout *layout,
                              const char label[12], uint32_t volume_id) {
    memset(sector, 0, 512);
    sector[0] = 0xeb; sector[1] = 0x58; sector[2] = 0x90;
    memcpy(sector + 3, "DEADBYTE", 8);
    put_le16(sector + 11, 512);
    sector[13] = (uint8_t)layout->sectors_per_cluster;
    put_le16(sector + 14, (uint16_t)layout->reserved_sectors);
    sector[16] = (uint8_t)layout->fat_count;
    put_le16(sector + 17, 0);
    put_le16(sector + 19, 0);
    sector[21] = 0xf8;
    put_le16(sector + 22, 0);
    put_le16(sector + 24, 63);
    put_le16(sector + 26, 255);
    put_le32(sector + 28, layout->partition_start_lba);
    put_le32(sector + 32, layout->partition_sectors);
    put_le32(sector + 36, layout->sectors_per_fat);
    put_le16(sector + 40, 0);
    put_le16(sector + 42, 0);
    put_le32(sector + 44, layout->root_cluster);
    put_le16(sector + 48, DF_FAT32_FSINFO_SECTOR);
    put_le16(sector + 50, DF_FAT32_BACKUP_BOOT_SECTOR);
    sector[64] = 0x80;
    sector[66] = 0x29;
    put_le32(sector + 67, volume_id);
    memcpy(sector + 71, label, 11);
    memcpy(sector + 82, "FAT32   ", 8);
    sector[510] = 0x55;
    sector[511] = 0xaa;
}

static void build_fsinfo(uint8_t sector[512]) {
    memset(sector, 0, 512);
    put_le32(sector + 0, 0x41615252u);
    put_le32(sector + 484, 0x61417272u);
    put_le32(sector + 488, 0xffffffffu);
    put_le32(sector + 492, 3u);
    put_le32(sector + 508, 0xaa550000u);
}

static df_status write_exact(df_file *file, const void *data, size_t length,
                             uint64_t offset, df_error *error) {
    size_t written = 0;
    df_status status = df_write_at(file, data, length, offset, &written, error);
    if (status != DF_OK) return status;
    if (written != length) {
        df_error_set(error, DF_ERR_IO, 0, "short FAT32 metadata write at offset %" PRIu64, offset);
        return DF_ERR_IO;
    }
    return DF_OK;
}

static df_status zero_region(df_file *file, uint64_t offset, uint64_t length,
                             df_error *error) {
    const size_t chunk_size = 1024u * 1024u;
    uint8_t *zero = (uint8_t *)df_aligned_alloc(4096, chunk_size);
    if (zero == NULL) {
        df_error_set(error, DF_ERR_NO_MEMORY, 0, "could not allocate FAT32 zero buffer");
        return DF_ERR_NO_MEMORY;
    }
    memset(zero, 0, chunk_size);
    while (length > 0) {
        size_t chunk = length < chunk_size ? (size_t)length : chunk_size;
        df_status status = write_exact(file, zero, chunk, offset, error);
        if (status != DF_OK) {
            df_aligned_free(zero);
            return status;
        }
        offset += chunk;
        length -= chunk;
    }
    df_aligned_free(zero);
    return DF_OK;
}

df_status df_format_fat32(const char *target_path, const df_fat32_options *options_in,
                          df_target_info *target_info, df_fat32_layout *layout_out,
                          df_error *error) {
    df_fat32_options options;
    df_write_options write_options;
    df_target_info info;
    df_fat32_layout layout;
    df_file target;
    df_status status;
    uint64_t target_size;
    uint64_t partition_offset;
    uint64_t metadata_bytes;
    uint64_t fat1_offset;
    uint64_t fat2_offset;
    uint64_t data_offset;
    uint8_t sector[512];
    char label[12];
    uint32_t volume_id;
    uint8_t fat_entries[512];
    uint8_t root_cluster[512];

    df_error_clear(error);
    if (target_path == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "FAT32 target path is required");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(&options, 0, sizeof(options));
    options.create_mbr = true;
    options.partition_start_lba = 2048;
    if (options_in != NULL) options = *options_in;
    if (options.partition_start_lba == 0) options.partition_start_lba = 2048;
    df_fat32_normalize_label(options.label, label);

    status = df_inspect_target(target_path, &info, error);
    if (status != DF_OK) return status;
    target_size = info.kind == DF_TARGET_REGULAR_FILE && options.regular_file_size != 0 ?
                  options.regular_file_size : info.size_bytes;
    status = df_fat32_compute_layout(target_size, &options, &layout, error);
    if (status != DF_OK) return status;

    memset(&write_options, 0, sizeof(write_options));
    write_options.allow_device = options.allow_device;
    write_options.force_mounted = options.force_mounted;
    write_options.force_system_disk = options.force_system_disk;
    write_options.confirmation_token = options.confirmation_token;
    write_options.truncate_regular_file = false;
    status = df_open_target(target_path, &info, &write_options, &target, error);
    if (status != DF_OK) return status;
    if (!target.is_device && target.size_bytes != target_size) {
        status = df_resize_regular_file(&target, target_size, error);
        if (status != DF_OK) goto out;
    }

    partition_offset = (uint64_t)layout.partition_start_lba * 512ULL;
    metadata_bytes = ((uint64_t)layout.reserved_sectors +
                      (uint64_t)layout.fat_count * layout.sectors_per_fat +
                      layout.sectors_per_cluster) * 512ULL;
    status = zero_region(&target, partition_offset, metadata_bytes, error);
    if (status != DF_OK) goto out;

    if (options.create_mbr) {
        build_mbr(sector, &layout);
        status = write_exact(&target, sector, sizeof(sector), 0, error);
        if (status != DF_OK) goto out;
    }

    volume_id = (uint32_t)(df_unix_time_ms() ^ target_size ^ (target_size >> 32));
    build_boot_sector(sector, &layout, label, volume_id);
    status = write_exact(&target, sector, sizeof(sector), partition_offset, error);
    if (status != DF_OK) goto out;
    status = write_exact(&target, sector, sizeof(sector),
                         partition_offset + (uint64_t)DF_FAT32_BACKUP_BOOT_SECTOR * 512ULL, error);
    if (status != DF_OK) goto out;

    build_fsinfo(sector);
    status = write_exact(&target, sector, sizeof(sector),
                         partition_offset + (uint64_t)DF_FAT32_FSINFO_SECTOR * 512ULL, error);
    if (status != DF_OK) goto out;
    status = write_exact(&target, sector, sizeof(sector),
                         partition_offset + (uint64_t)(DF_FAT32_BACKUP_BOOT_SECTOR + 1u) * 512ULL, error);
    if (status != DF_OK) goto out;

    memset(fat_entries, 0, sizeof(fat_entries));
    put_le32(fat_entries + 0, 0x0ffffff8u);
    put_le32(fat_entries + 4, 0x0fffffffu);
    put_le32(fat_entries + 8, 0x0fffffffu);
    fat1_offset = partition_offset + (uint64_t)layout.reserved_sectors * 512ULL;
    fat2_offset = fat1_offset + (uint64_t)layout.sectors_per_fat * 512ULL;
    status = write_exact(&target, fat_entries, sizeof(fat_entries), fat1_offset, error);
    if (status != DF_OK) goto out;
    status = write_exact(&target, fat_entries, sizeof(fat_entries), fat2_offset, error);
    if (status != DF_OK) goto out;

    memset(root_cluster, 0, sizeof(root_cluster));
    memcpy(root_cluster, label, 11);
    root_cluster[11] = 0x08;
    data_offset = partition_offset +
                  ((uint64_t)layout.reserved_sectors +
                   (uint64_t)layout.fat_count * layout.sectors_per_fat) * 512ULL;
    status = write_exact(&target, root_cluster, sizeof(root_cluster), data_offset, error);
    if (status != DF_OK) goto out;

    status = df_flush(&target, error);
    if (status != DF_OK) goto out;
    if (target_info != NULL) {
        df_target_info refreshed;
        if (df_inspect_target(target_path, &refreshed, NULL) == DF_OK) *target_info = refreshed;
        else *target_info = info;
    }
    if (layout_out != NULL) *layout_out = layout;

out:
    df_close_file(&target);
    return status;
}

df_status df_verify_fat32(const char *target_path, df_fat32_layout *layout,
                          char label[12], df_error *error) {
    df_file target;
    df_target_info info;
    df_status status;
    uint8_t mbr[512];
    uint8_t boot[512];
    size_t got = 0;
    uint32_t start_lba;
    uint32_t sectors;
    uint32_t fatsz;
    uint32_t spc;
    uint32_t reserved;
    uint32_t fats;
    uint32_t clusters;

    df_error_clear(error);
    status = df_inspect_target(target_path, &info, error);
    if (status != DF_OK) return status;
    memset(&target, 0, sizeof(target));
    target.handle = DF_INVALID_HANDLE;
    status = df_open_source(target_path, &target, error);
    if (status != DF_OK) return status;
    status = df_read_at(&target, mbr, sizeof(mbr), 0, &got, error);
    if (status != DF_OK || got != sizeof(mbr)) goto bad;
    if (mbr[510] != 0x55 || mbr[511] != 0xaa || mbr[446 + 4] != 0x0c) goto bad;
    start_lba = get_le32(mbr + 446 + 8);
    sectors = get_le32(mbr + 446 + 12);
    status = df_read_at(&target, boot, sizeof(boot), (uint64_t)start_lba * 512ULL, &got, error);
    if (status != DF_OK || got != sizeof(boot)) goto bad;
    if (boot[510] != 0x55 || boot[511] != 0xaa ||
        get_le16(boot + 11) != 512 || memcmp(boot + 82, "FAT32   ", 8) != 0) goto bad;
    spc = boot[13];
    reserved = get_le16(boot + 14);
    fats = boot[16];
    fatsz = get_le32(boot + 36);
    if (spc == 0 || reserved < 1 || fats != 2 || fatsz == 0 ||
        sectors <= reserved + fats * fatsz) goto bad;
    clusters = (sectors - reserved - fats * fatsz) / spc;
    if (clusters < DF_FAT32_MIN_CLUSTERS || clusters >= DF_FAT32_MAX_CLUSTERS) goto bad;
    if (layout != NULL) {
        memset(layout, 0, sizeof(*layout));
        layout->bytes_per_sector = 512;
        layout->sectors_per_cluster = spc;
        layout->reserved_sectors = reserved;
        layout->fat_count = fats;
        layout->sectors_per_fat = fatsz;
        layout->partition_start_lba = start_lba;
        layout->partition_sectors = sectors;
        layout->data_clusters = clusters;
        layout->root_cluster = get_le32(boot + 44);
    }
    if (label != NULL) {
        memcpy(label, boot + 71, 11);
        label[11] = '\0';
    }
    df_close_file(&target);
    return DF_OK;

bad:
    if (status == DF_OK) df_error_set(error, DF_ERR_FORMAT, 0, "target does not contain a valid DEADFLASH MBR/FAT32 layout");
    df_close_file(&target);
    return status == DF_OK ? DF_ERR_FORMAT : status;
}
