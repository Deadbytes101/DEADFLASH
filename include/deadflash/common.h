#ifndef DEADFLASH_COMMON_H
#define DEADFLASH_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DF_VERSION_MAJOR 1
#define DF_VERSION_MINOR 0
#define DF_VERSION_PATCH 0
#define DF_VERSION_STRING "1.0.0"

#define DF_DEFAULT_BUFFER_SIZE (32u * 1024u * 1024u)
#define DF_MAX_PATH_CHARS 4096u
#define DF_TOKEN_HEX_CHARS 16u
#define DF_SHA256_HEX_CHARS 64u
#define DF_MAX_ERROR_MESSAGE 512u

#if defined(_WIN32)
#define DF_PATH_SEP '\\'
#else
#define DF_PATH_SEP '/'
#endif

typedef enum df_status {
    DF_OK = 0,
    DF_ERR_INVALID_ARGUMENT,
    DF_ERR_OPEN,
    DF_ERR_IO,
    DF_ERR_PERMISSION,
    DF_ERR_UNSUPPORTED,
    DF_ERR_NO_MEMORY,
    DF_ERR_TOO_SMALL,
    DF_ERR_TOO_LARGE,
    DF_ERR_SOURCE_TOO_LARGE,
    DF_ERR_DEVICE_REQUIRED,
    DF_ERR_DEVICE_FORBIDDEN,
    DF_ERR_SYSTEM_DISK,
    DF_ERR_MOUNTED,
    DF_ERR_CONFIRMATION,
    DF_ERR_IDENTITY_CHANGED,
    DF_ERR_VERIFY_MISMATCH,
    DF_ERR_FORMAT,
    DF_ERR_CANCELLED,
    DF_ERR_INTERNAL
} df_status;

typedef struct df_error {
    df_status code;
    int os_code;
    char message[DF_MAX_ERROR_MESSAGE];
} df_error;

typedef enum df_verify_mode {
    DF_VERIFY_NONE = 0,
    DF_VERIFY_SAMPLE,
    DF_VERIFY_FULL
} df_verify_mode;

typedef struct df_timer {
    uint64_t start_ns;
} df_timer;

const char *df_status_name(df_status status);
void df_error_clear(df_error *error);
void df_error_set(df_error *error, df_status code, int os_code, const char *fmt, ...);
uint64_t df_monotonic_ns(void);
uint64_t df_unix_time_ms(void);
void df_timer_start(df_timer *timer);
double df_timer_elapsed_ms(const df_timer *timer);
void *df_aligned_alloc(size_t alignment, size_t size);
void df_aligned_free(void *ptr);
bool df_parse_u64(const char *text, uint64_t *value);
bool df_parse_size(const char *text, uint64_t *bytes);
const char *df_path_basename(const char *path);
void df_hex_encode(const uint8_t *bytes, size_t length, char *output);
int df_constant_time_equal(const void *a, const void *b, size_t length);

#endif
