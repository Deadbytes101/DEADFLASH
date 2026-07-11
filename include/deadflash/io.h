#ifndef DEADFLASH_IO_H
#define DEADFLASH_IO_H

#include "deadflash/common.h"

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE df_native_handle;
#define DF_INVALID_HANDLE INVALID_HANDLE_VALUE
#else
typedef int df_native_handle;
#define DF_INVALID_HANDLE (-1)
#endif

#define DF_DEVICE_BUS_CHARS 32u
#define DF_DEVICE_VENDOR_CHARS 64u
#define DF_DEVICE_PRODUCT_CHARS 128u
#define DF_DEVICE_REVISION_CHARS 32u

typedef enum df_target_kind {
    DF_TARGET_REGULAR_FILE = 0,
    DF_TARGET_BLOCK_DEVICE,
    DF_TARGET_CHAR_DEVICE,
    DF_TARGET_UNKNOWN
} df_target_kind;

typedef enum df_progress_phase {
    DF_PROGRESS_PREPARE = 0,
    DF_PROGRESS_HASH_SOURCE,
    DF_PROGRESS_WRITE,
    DF_PROGRESS_FLUSH,
    DF_PROGRESS_VERIFY,
    DF_PROGRESS_COMPLETE
} df_progress_phase;

typedef void (*df_progress_callback)(void *context,
                                     df_progress_phase phase,
                                     uint64_t completed,
                                     uint64_t total);

typedef struct df_target_info {
    char path[DF_MAX_PATH_CHARS];
    char display_name[256];
    df_target_kind kind;
    uint64_t size_bytes;
    uint32_t logical_sector_size;
    uint32_t physical_sector_size;
    bool removable;
    bool read_only;
    bool mounted;
    bool system_disk;
    bool descriptor_present;
    bool serial_bound;
    char bus_type[DF_DEVICE_BUS_CHARS];
    char vendor[DF_DEVICE_VENDOR_CHARS];
    char product[DF_DEVICE_PRODUCT_CHARS];
    char revision[DF_DEVICE_REVISION_CHARS];
    char serial_sha256[DF_SHA256_HEX_CHARS + 1];
    char token[DF_TOKEN_HEX_CHARS + 1];
} df_target_info;

typedef struct df_file {
    df_native_handle handle;
    char path[DF_MAX_PATH_CHARS];
    uint64_t size_bytes;
    uint32_t alignment;
    bool writable;
    bool direct_io;
    bool is_device;
#if defined(_WIN32)
    HANDLE locked_volumes[26];
    unsigned locked_volume_count;
#endif
} df_file;

typedef struct df_write_options {
    size_t buffer_size;
    unsigned write_retries;
    df_verify_mode verify_mode;
    unsigned sample_count;
    bool allow_device;
    bool force_mounted;
    bool force_system_disk;
    bool direct_io;
    bool truncate_regular_file;
    const char *confirmation_token;
    df_progress_callback progress_callback;
    void *progress_context;
} df_write_options;

typedef struct df_write_result {
    uint64_t source_size;
    uint64_t bytes_written;
    uint64_t bytes_verified;
    uint64_t write_retries;
    uint64_t verification_mismatches;
    double source_hash_ms;
    double write_ms;
    double flush_ms;
    double verify_ms;
    double total_ms;
    uint8_t source_sha256[32];
    uint8_t target_sha256[32];
    char final_state[48];
} df_write_result;

const char *df_progress_phase_name(df_progress_phase phase);
void df_compute_target_token(const df_target_info *info, char token[DF_TOKEN_HEX_CHARS + 1]);
df_status df_inspect_target(const char *path, df_target_info *info, df_error *error);
df_status df_list_targets(FILE *output, df_error *error);
df_status df_open_source(const char *path, df_file *file, df_error *error);
df_status df_open_target(const char *path, const df_target_info *info, const df_write_options *options,
                         df_file *file, df_error *error);
void df_close_file(df_file *file);
df_status df_read_at(df_file *file, void *buffer, size_t length, uint64_t offset,
                     size_t *bytes_read, df_error *error);
df_status df_write_at(df_file *file, const void *buffer, size_t length, uint64_t offset,
                      size_t *bytes_written, df_error *error);
df_status df_flush(df_file *file, df_error *error);
df_status df_resize_regular_file(df_file *file, uint64_t size, df_error *error);
df_status df_hash_file_region(df_file *file, uint64_t length, size_t buffer_size,
                              uint8_t digest[32], df_error *error);
df_status df_hash_source_path(const char *path, size_t buffer_size,
                              uint64_t *size_bytes, uint8_t digest[32],
                              df_progress_callback callback, void *context,
                              df_error *error);
df_status df_write_image(const char *source_path, const char *target_path,
                         const df_write_options *options, df_target_info *target_info,
                         df_write_result *result, df_error *error);
df_status df_verify_image(const char *source_path, const char *target_path,
                          df_verify_mode mode, unsigned sample_count, size_t buffer_size,
                          df_write_result *result, df_error *error);

#endif
