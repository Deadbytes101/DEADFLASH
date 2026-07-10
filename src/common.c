#include "deadflash/common.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <malloc.h>
#include <windows.h>
#else
#include <sys/time.h>
#endif

const char *df_status_name(df_status status) {
    switch (status) {
        case DF_OK: return "ok";
        case DF_ERR_INVALID_ARGUMENT: return "invalid_argument";
        case DF_ERR_OPEN: return "open_failed";
        case DF_ERR_IO: return "io_failed";
        case DF_ERR_PERMISSION: return "permission_denied";
        case DF_ERR_UNSUPPORTED: return "unsupported";
        case DF_ERR_NO_MEMORY: return "out_of_memory";
        case DF_ERR_TOO_SMALL: return "target_too_small";
        case DF_ERR_TOO_LARGE: return "target_too_large";
        case DF_ERR_SOURCE_TOO_LARGE: return "source_too_large";
        case DF_ERR_DEVICE_REQUIRED: return "device_required";
        case DF_ERR_DEVICE_FORBIDDEN: return "device_forbidden";
        case DF_ERR_SYSTEM_DISK: return "system_disk_rejected";
        case DF_ERR_MOUNTED: return "mounted_target_rejected";
        case DF_ERR_CONFIRMATION: return "confirmation_failed";
        case DF_ERR_IDENTITY_CHANGED: return "identity_changed";
        case DF_ERR_VERIFY_MISMATCH: return "verification_mismatch";
        case DF_ERR_FORMAT: return "format_failed";
        case DF_ERR_CANCELLED: return "cancelled";
        case DF_ERR_INTERNAL: return "internal_error";
        default: return "unknown_error";
    }
}

void df_error_clear(df_error *error) {
    if (error == NULL) return;
    error->code = DF_OK;
    error->os_code = 0;
    error->message[0] = '\0';
}

void df_error_set(df_error *error, df_status code, int os_code, const char *fmt, ...) {
    va_list args;
    if (error == NULL) return;
    error->code = code;
    error->os_code = os_code;
    if (fmt == NULL) {
        error->message[0] = '\0';
        return;
    }
    va_start(args, fmt);
    (void)vsnprintf(error->message, sizeof(error->message), fmt, args);
    va_end(args);
    error->message[sizeof(error->message) - 1] = '\0';
}

uint64_t df_monotonic_ns(void) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    static BOOL initialized = FALSE;
    LARGE_INTEGER counter;
    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = TRUE;
    }
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / (uint64_t)frequency.QuadPart);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

uint64_t df_unix_time_ms(void) {
#if defined(_WIN32)
    FILETIME ft;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&ft);
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

void df_timer_start(df_timer *timer) {
    if (timer != NULL) timer->start_ns = df_monotonic_ns();
}

double df_timer_elapsed_ms(const df_timer *timer) {
    uint64_t now;
    if (timer == NULL || timer->start_ns == 0) return 0.0;
    now = df_monotonic_ns();
    if (now < timer->start_ns) return 0.0;
    return (double)(now - timer->start_ns) / 1000000.0;
}

void *df_aligned_alloc(size_t alignment, size_t size) {
    void *ptr = NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if ((alignment & (alignment - 1u)) != 0u) return NULL;
#if defined(_WIN32)
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) ptr = NULL;
#endif
    return ptr;
}

void df_aligned_free(void *ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

bool df_parse_u64(const char *text, uint64_t *value) {
    char *end = NULL;
    unsigned long long parsed;
    if (text == NULL || value == NULL || *text == '\0' || *text == '-') return false;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    *value = (uint64_t)parsed;
    return true;
}

bool df_parse_size(const char *text, uint64_t *bytes) {
    char *end = NULL;
    unsigned long long value;
    uint64_t multiplier = 1;
    if (text == NULL || bytes == NULL || *text == '\0' || *text == '-') return false;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text) return false;
    while (isspace((unsigned char)*end)) ++end;
    if (*end != '\0') {
        char unit = (char)toupper((unsigned char)*end++);
        if (*end == 'i' || *end == 'I') ++end;
        if (*end == 'b' || *end == 'B') ++end;
        while (isspace((unsigned char)*end)) ++end;
        if (*end != '\0') return false;
        switch (unit) {
            case 'K': multiplier = 1024ULL; break;
            case 'M': multiplier = 1024ULL * 1024ULL; break;
            case 'G': multiplier = 1024ULL * 1024ULL * 1024ULL; break;
            case 'T': multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL; break;
            default: return false;
        }
    }
    if ((uint64_t)value > UINT64_MAX / multiplier) return false;
    *bytes = (uint64_t)value * multiplier;
    return true;
}

const char *df_path_basename(const char *path) {
    const char *last = path;
    const char *p;
    if (path == NULL) return "";
    for (p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return last;
}

void df_hex_encode(const uint8_t *bytes, size_t length, char *output) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    if (bytes == NULL || output == NULL) return;
    for (i = 0; i < length; ++i) {
        output[i * 2] = hex[bytes[i] >> 4];
        output[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    output[length * 2] = '\0';
}

int df_constant_time_equal(const void *a, const void *b, size_t length) {
    const uint8_t *left = (const uint8_t *)a;
    const uint8_t *right = (const uint8_t *)b;
    uint8_t diff = 0;
    size_t i;
    if (a == NULL || b == NULL) return 0;
    for (i = 0; i < length; ++i) diff |= (uint8_t)(left[i] ^ right[i]);
    return diff == 0;
}
