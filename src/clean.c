#include "deadflash/clean.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <ntdddisk.h>
#include <winioctl.h>
#endif

typedef struct df_clean_layout_snapshot {
    df_clean_partition_style style;
    unsigned partition_count;
    uint32_t mbr_signature;
#if defined(_WIN32)
    GUID gpt_disk_id;
#endif
} df_clean_layout_snapshot;

const char *df_clean_partition_style_name(df_clean_partition_style style) {
    switch (style) {
        case DF_CLEAN_PARTITION_RAW: return "raw";
        case DF_CLEAN_PARTITION_MBR: return "mbr";
        case DF_CLEAN_PARTITION_GPT: return "gpt";
        case DF_CLEAN_PARTITION_UNKNOWN:
        default: return "unknown";
    }
}

static void df_clean_result_reset(df_clean_result *result) {
    if (result == NULL) return;
    memset(result, 0, sizeof(*result));
    (void)snprintf(result->final_state,
                   sizeof(result->final_state),
                   "%s", "failed_before_clean");
}

static void df_clean_set_state(df_clean_result *result,
                               const char *state) {
    if (result == NULL || state == NULL) return;
    (void)snprintf(result->final_state,
                   sizeof(result->final_state),
                   "%s", state);
}

#if defined(_WIN32)

static bool df_clean_parse_physical_drive(const char *path,
                                          unsigned *disk_number) {
    static const char prefix1[] = "\\\\.\\PhysicalDrive";
    static const char prefix2[] = "//./PhysicalDrive";
    const char *digits = NULL;
    char *end = NULL;
    unsigned long value;

    if (path == NULL) return false;
    if (_strnicmp(path, prefix1, sizeof(prefix1) - 1u) == 0) {
        digits = path + sizeof(prefix1) - 1u;
    } else if (_strnicmp(path, prefix2, sizeof(prefix2) - 1u) == 0) {
        digits = path + sizeof(prefix2) - 1u;
    } else {
        return false;
    }
    if (*digits == '\0') return false;
    value = strtoul(digits, &end, 10);
    if (end == digits || *end != '\0' || value > 255u) return false;
    if (disk_number != NULL) *disk_number = (unsigned)value;
    return true;
}

static df_clean_partition_style df_clean_map_partition_style(
    PARTITION_STYLE style) {
    switch (style) {
        case PARTITION_STYLE_RAW: return DF_CLEAN_PARTITION_RAW;
        case PARTITION_STYLE_MBR: return DF_CLEAN_PARTITION_MBR;
        case PARTITION_STYLE_GPT: return DF_CLEAN_PARTITION_GPT;
        default: return DF_CLEAN_PARTITION_UNKNOWN;
    }
}

static unsigned df_clean_count_partitions(
    const DRIVE_LAYOUT_INFORMATION_EX *layout,
    DWORD returned) {
    const size_t header = offsetof(DRIVE_LAYOUT_INFORMATION_EX,
                                   PartitionEntry);
    size_t available;
    size_t limit;
    size_t index;
    unsigned count = 0u;

    if (layout == NULL || (size_t)returned < header) return 0u;
    available = ((size_t)returned - header) /
                sizeof(PARTITION_INFORMATION_EX);
    limit = (size_t)layout->PartitionCount;
    if (limit > available) limit = available;

    for (index = 0u; index < limit; ++index) {
        const PARTITION_INFORMATION_EX *entry =
            &layout->PartitionEntry[index];
        if (entry->PartitionNumber == 0u ||
            entry->PartitionLength.QuadPart <= 0) {
            continue;
        }
        if (entry->PartitionStyle == PARTITION_STYLE_MBR &&
            entry->Info.Mbr.PartitionType == PARTITION_ENTRY_UNUSED) {
            continue;
        }
        ++count;
    }
    return count;
}

static df_status df_clean_query_layout_handle(
    HANDLE handle,
    df_clean_layout_snapshot *snapshot,
    df_error *error) {
    DWORD capacity = 4096u;

    if (handle == INVALID_HANDLE_VALUE || snapshot == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "invalid disk layout query");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->style = DF_CLEAN_PARTITION_UNKNOWN;

    while (capacity <= 1024u * 1024u) {
        BYTE *buffer = (BYTE *)calloc(1u, (size_t)capacity);
        DWORD returned = 0u;
        DWORD code;
        DRIVE_LAYOUT_INFORMATION_EX *layout;

        if (buffer == NULL) {
            df_error_set(error, DF_ERR_NO_MEMORY, 0,
                         "could not allocate disk layout buffer");
            return DF_ERR_NO_MEMORY;
        }
        if (DeviceIoControl(handle,
                            IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                            NULL, 0u,
                            buffer, capacity,
                            &returned, NULL)) {
            layout = (DRIVE_LAYOUT_INFORMATION_EX *)(void *)buffer;
            snapshot->style =
                df_clean_map_partition_style(layout->PartitionStyle);
            snapshot->partition_count =
                df_clean_count_partitions(layout, returned);
            if (layout->PartitionStyle == PARTITION_STYLE_MBR) {
                snapshot->mbr_signature = layout->Info.Mbr.Signature;
            } else if (layout->PartitionStyle == PARTITION_STYLE_GPT) {
                snapshot->gpt_disk_id = layout->Info.Gpt.DiskId;
            }
            free(buffer);
            return DF_OK;
        }

        code = GetLastError();
        free(buffer);
        if (code == ERROR_INSUFFICIENT_BUFFER ||
            code == ERROR_MORE_DATA) {
            capacity *= 2u;
            continue;
        }
        if (code == ERROR_INVALID_FUNCTION ||
            code == ERROR_NOT_READY) {
            snapshot->style = DF_CLEAN_PARTITION_RAW;
            snapshot->partition_count = 0u;
            return DF_OK;
        }
        df_error_set(error, DF_ERR_IO, (int)code,
                     "query disk partition layout failed");
        return DF_ERR_IO;
    }

    df_error_set(error, DF_ERR_TOO_LARGE, 0,
                 "disk partition layout exceeded the bounded query buffer");
    return DF_ERR_TOO_LARGE;
}

static df_status df_clean_open_probe(const char *path,
                                     HANDLE *handle,
                                     df_error *error) {
    HANDLE opened;
    if (path == NULL || handle == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "invalid disk probe arguments");
        return DF_ERR_INVALID_ARGUMENT;
    }
    *handle = INVALID_HANDLE_VALUE;
    opened = CreateFileA(path, GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE |
                             FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (opened == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        df_error_set(error, DF_ERR_OPEN, (int)code,
                     "open physical disk for layout inspection failed");
        return DF_ERR_OPEN;
    }
    *handle = opened;
    return DF_OK;
}

static df_status df_clean_query_layout_path(
    const char *path,
    df_clean_layout_snapshot *snapshot,
    df_error *error) {
    HANDLE handle = INVALID_HANDLE_VALUE;
    df_status status = df_clean_open_probe(path, &handle, error);
    if (status != DF_OK) return status;
    status = df_clean_query_layout_handle(handle, snapshot, error);
    CloseHandle(handle);
    return status;
}

static bool df_clean_guid_equal(const GUID *left,
                                const GUID *right) {
    return left != NULL && right != NULL &&
           memcmp(left, right, sizeof(*left)) == 0;
}

static bool df_clean_layout_equal(
    const df_clean_layout_snapshot *left,
    const df_clean_layout_snapshot *right) {
    if (left == NULL || right == NULL) return false;
    if (left->style != right->style ||
        left->partition_count != right->partition_count) {
        return false;
    }
    if (left->style == DF_CLEAN_PARTITION_MBR) {
        return left->mbr_signature == right->mbr_signature;
    }
    if (left->style == DF_CLEAN_PARTITION_GPT) {
        return df_clean_guid_equal(&left->gpt_disk_id,
                                   &right->gpt_disk_id);
    }
    return true;
}

static bool df_clean_disk_for_drive_letter(wchar_t letter,
                                           DWORD *disk_number) {
    wchar_t path[] = L"\\\\.\\C:";
    STORAGE_DEVICE_NUMBER number;
    HANDLE handle;
    DWORD returned = 0u;

    path[4] = letter;
    handle = CreateFileW(path, 0u,
                         FILE_SHARE_READ | FILE_SHARE_WRITE |
                             FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0u, NULL);
    if (handle == INVALID_HANDLE_VALUE) return false;
    memset(&number, 0, sizeof(number));
    if (!DeviceIoControl(handle,
                         IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         NULL, 0u,
                         &number, (DWORD)sizeof(number),
                         &returned, NULL)) {
        CloseHandle(handle);
        return false;
    }
    CloseHandle(handle);
    if (disk_number != NULL) *disk_number = number.DeviceNumber;
    return true;
}

static bool df_clean_program_disk(DWORD *disk_number) {
    wchar_t path[DF_MAX_PATH_CHARS];
    DWORD length = GetModuleFileNameW(
        NULL, path,
        (DWORD)(sizeof(path) / sizeof(path[0])));
    if (length < 2u || length >=
            (DWORD)(sizeof(path) / sizeof(path[0])) ||
        path[1] != L':') {
        return false;
    }
    return df_clean_disk_for_drive_letter(path[0], disk_number);
}

static bool df_clean_is_usb_target(const df_target_info *target) {
    return target != NULL &&
           strcmp(target->bus_type, "windows:7") == 0;
}

static df_status df_clean_validate_target(
    const df_target_info *target,
    unsigned disk_number,
    const char *confirmation_token,
    df_error *error) {
    DWORD program_disk = 0u;

    if (target->kind != DF_TARGET_BLOCK_DEVICE) {
        df_error_set(error, DF_ERR_DEVICE_REQUIRED, 0,
                     "clean requires a whole physical disk");
        return DF_ERR_DEVICE_REQUIRED;
    }
    if (!target->removable || !df_clean_is_usb_target(target)) {
        df_error_set(error, DF_ERR_DEVICE_FORBIDDEN, 0,
                     "clean is restricted to removable USB disks");
        return DF_ERR_DEVICE_FORBIDDEN;
    }
    if (target->read_only) {
        df_error_set(error, DF_ERR_PERMISSION, 0,
                     "target disk is read-only");
        return DF_ERR_PERMISSION;
    }
    if (target->system_disk) {
        df_error_set(error, DF_ERR_SYSTEM_DISK, 0,
                     "the Windows system disk cannot be cleaned");
        return DF_ERR_SYSTEM_DISK;
    }
    if (!df_clean_program_disk(&program_disk)) {
        df_error_set(error, DF_ERR_INTERNAL, 0,
                     "could not prove which disk contains DEADFLASH");
        return DF_ERR_INTERNAL;
    }
    if (program_disk == (DWORD)disk_number) {
        df_error_set(error, DF_ERR_SYSTEM_DISK, 0,
                     "the disk containing DEADFLASH cannot be cleaned");
        return DF_ERR_SYSTEM_DISK;
    }
    if (confirmation_token == NULL ||
        strcmp(confirmation_token, target->token) != 0) {
        df_error_set(error, DF_ERR_CONFIRMATION, 0,
                     "clean confirmation token mismatch; expected %s",
                     target->token);
        return DF_ERR_CONFIRMATION;
    }
    return DF_OK;
}

#endif

df_status df_clean_disk(const char *target_path,
                        const char *confirmation_token,
                        df_clean_result *result,
                        df_error *error) {
    df_timer total_timer;

    df_clean_result_reset(result);
    df_error_clear(error);
    df_timer_start(&total_timer);

    if (target_path == NULL || target_path[0] == '\0' ||
        result == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "target path and clean result are required");
        return DF_ERR_INVALID_ARGUMENT;
    }

#if defined(_WIN32)
    {
        unsigned disk_number = 0u;
        DWORD returned = 0u;
        df_clean_layout_snapshot planned_layout;
        df_clean_layout_snapshot locked_layout;
        df_clean_layout_snapshot verified_layout;
        df_write_options options;
        df_file target_file;
        df_timer clean_timer;
        df_timer verify_timer;
        df_status status;
        unsigned attempt;

        if (!df_clean_parse_physical_drive(target_path,
                                           &disk_number)) {
            df_error_set(error, DF_ERR_DEVICE_REQUIRED, 0,
                         "clean requires a PhysicalDrive path");
            return DF_ERR_DEVICE_REQUIRED;
        }
        result->disk_number = disk_number;

        status = df_inspect_target(target_path,
                                   &result->before_target,
                                   error);
        if (status != DF_OK) {
            result->total_ms = df_timer_elapsed_ms(&total_timer);
            return status;
        }
        status = df_clean_validate_target(&result->before_target,
                                          disk_number,
                                          confirmation_token,
                                          error);
        if (status != DF_OK) {
            result->total_ms = df_timer_elapsed_ms(&total_timer);
            return status;
        }
        status = df_clean_query_layout_path(target_path,
                                            &planned_layout,
                                            error);
        if (status != DF_OK) {
            result->total_ms = df_timer_elapsed_ms(&total_timer);
            return status;
        }
        result->before_style = planned_layout.style;
        result->before_partition_count =
            planned_layout.partition_count;

        if (planned_layout.style == DF_CLEAN_PARTITION_RAW &&
            planned_layout.partition_count == 0u &&
            !result->before_target.mounted) {
            result->after_style = planned_layout.style;
            result->after_partition_count = 0u;
            result->after_target = result->before_target;
            df_clean_set_state(result, "success_already_clean");
            result->total_ms = df_timer_elapsed_ms(&total_timer);
            return DF_OK;
        }

        memset(&options, 0, sizeof(options));
        options.allow_device = true;
        options.force_mounted = true;
        options.confirmation_token = confirmation_token;
        memset(&target_file, 0, sizeof(target_file));
        target_file.handle = DF_INVALID_HANDLE;

        status = df_open_target(target_path,
                                &result->before_target,
                                &options,
                                &target_file,
                                error);
        if (status != DF_OK) {
            result->total_ms = df_timer_elapsed_ms(&total_timer);
            return status;
        }

        status = df_clean_query_layout_handle(target_file.handle,
                                              &locked_layout,
                                              error);
        if (status != DF_OK ||
            !df_clean_layout_equal(&planned_layout,
                                   &locked_layout)) {
            if (status == DF_OK) {
                df_error_set(error, DF_ERR_IDENTITY_CHANGED, 0,
                             "partition layout changed before clean");
                status = DF_ERR_IDENTITY_CHANGED;
            }
            df_close_file(&target_file);
            result->total_ms = df_timer_elapsed_ms(&total_timer);
            return status;
        }

        df_clean_set_state(result, "failed_during_clean");
        df_timer_start(&clean_timer);
        if (!DeviceIoControl(target_file.handle,
                             IOCTL_DISK_DELETE_DRIVE_LAYOUT,
                             NULL, 0u, NULL, 0u,
                             &returned, NULL)) {
            const DWORD code = GetLastError();
            df_error_set(error, DF_ERR_IO, (int)code,
                         "delete disk partition layout failed");
            df_close_file(&target_file);
            result->clean_ms = df_timer_elapsed_ms(&clean_timer);
            result->total_ms = df_timer_elapsed_ms(&total_timer);
            return DF_ERR_IO;
        }
        result->layout_deleted = true;
        result->clean_ms = df_timer_elapsed_ms(&clean_timer);
        (void)DeviceIoControl(target_file.handle,
                              IOCTL_DISK_UPDATE_PROPERTIES,
                              NULL, 0u, NULL, 0u,
                              &returned, NULL);
        df_close_file(&target_file);

        df_clean_set_state(result, "clean_unverified");
        df_timer_start(&verify_timer);
        for (attempt = 0u; attempt < 20u; ++attempt) {
            df_target_info after_target;
            df_error attempt_error;
            df_error_clear(&attempt_error);
            if (df_clean_query_layout_path(target_path,
                                           &verified_layout,
                                           &attempt_error) == DF_OK &&
                df_inspect_target(target_path,
                                  &after_target,
                                  &attempt_error) == DF_OK &&
                strcmp(after_target.token,
                       result->before_target.token) == 0 &&
                verified_layout.style == DF_CLEAN_PARTITION_RAW &&
                verified_layout.partition_count == 0u &&
                !after_target.mounted) {
                result->after_target = after_target;
                result->after_style = verified_layout.style;
                result->after_partition_count = 0u;
                result->verify_ms =
                    df_timer_elapsed_ms(&verify_timer);
                result->total_ms =
                    df_timer_elapsed_ms(&total_timer);
                df_clean_set_state(result,
                                   "success_clean_verified");
                df_error_clear(error);
                return DF_OK;
            }
            Sleep(250u);
        }

        result->verify_ms = df_timer_elapsed_ms(&verify_timer);
        result->total_ms = df_timer_elapsed_ms(&total_timer);
        result->after_style = verified_layout.style;
        result->after_partition_count =
            verified_layout.partition_count;
        df_error_set(error, DF_ERR_VERIFY_MISMATCH, 0,
                     "disk clean could not be verified as RAW with zero partitions and zero mounts");
        return DF_ERR_VERIFY_MISMATCH;
    }
#else
    (void)confirmation_token;
    result->total_ms = df_timer_elapsed_ms(&total_timer);
    df_error_set(error, DF_ERR_UNSUPPORTED, 0,
                 "whole-disk clean is currently implemented on Windows only");
    return DF_ERR_UNSUPPORTED;
#endif
}

static void df_clean_json_string(FILE *file, const char *text) {
    const unsigned char *cursor =
        (const unsigned char *)(text != NULL ? text : "");
    fputc('"', file);
    while (*cursor != '\0') {
        switch (*cursor) {
            case '"': fputs("\\\"", file); break;
            case '\\': fputs("\\\\", file); break;
            case '\n': fputs("\\n", file); break;
            case '\r': fputs("\\r", file); break;
            case '\t': fputs("\\t", file); break;
            default:
                if (*cursor < 0x20u) {
                    fprintf(file, "\\u%04x", (unsigned)*cursor);
                } else {
                    fputc((int)*cursor, file);
                }
                break;
        }
        ++cursor;
    }
    fputc('"', file);
}

static void df_clean_write_target_json(FILE *file,
                                       const df_target_info *target) {
    if (target == NULL) {
        fputs("null", file);
        return;
    }
    fputs("{\n", file);
    fprintf(file, "      \"kind\": %u,\n", (unsigned)target->kind);
    fprintf(file, "      \"size_bytes\": %" PRIu64 ",\n",
            target->size_bytes);
    fprintf(file, "      \"removable\": %s,\n",
            target->removable ? "true" : "false");
    fprintf(file, "      \"read_only\": %s,\n",
            target->read_only ? "true" : "false");
    fprintf(file, "      \"mounted\": %s,\n",
            target->mounted ? "true" : "false");
    fprintf(file, "      \"system_disk\": %s,\n",
            target->system_disk ? "true" : "false");
    fputs("      \"bus_type\": ", file);
    df_clean_json_string(file, target->bus_type);
    fputs(",\n      \"vendor\": ", file);
    df_clean_json_string(file, target->vendor);
    fputs(",\n      \"product\": ", file);
    df_clean_json_string(file, target->product);
    fputs(",\n      \"revision\": ", file);
    df_clean_json_string(file, target->revision);
    fputs(",\n      \"serial_sha256\": ", file);
    df_clean_json_string(file, target->serial_sha256);
    fputs(",\n      \"token\": ", file);
    df_clean_json_string(file, target->token);
    fputs("\n    }", file);
}

df_status df_write_clean_json_report(const char *path,
                                     const char *target_path,
                                     const df_clean_result *result,
                                     df_status status,
                                     const df_error *operation_error,
                                     df_error *report_error) {
    FILE *file;

    if (path == NULL || result == NULL) {
        df_error_set(report_error, DF_ERR_INVALID_ARGUMENT, 0,
                     "clean report path and result are required");
        return DF_ERR_INVALID_ARGUMENT;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        df_error_set(report_error, DF_ERR_OPEN, 0,
                     "could not create clean report '%s'", path);
        return DF_ERR_OPEN;
    }

    fputs("{\n  \"schema\": \"deadflash.clean.evidence.v1\",\n", file);
    fputs("  \"tool\": {\"name\": \"DEADFLASH\", \"version\": \""
          DF_VERSION_STRING "\"},\n", file);
    fprintf(file, "  \"timestamp_unix_ms\": %" PRIu64 ",\n",
            df_unix_time_ms());
    fputs("  \"operation\": \"clean_disk\",\n", file);
    fputs("  \"status\": ", file);
    df_clean_json_string(file, df_status_name(status));
    fputs(",\n  \"target_path\": ", file);
    df_clean_json_string(file, target_path);
    fputs(",\n  \"method\": \"IOCTL_DISK_DELETE_DRIVE_LAYOUT\",\n", file);
    fputs("  \"clean_all\": false,\n", file);
    fputs("  \"before_target\": ", file);
    df_clean_write_target_json(file, &result->before_target);
    fputs(",\n  \"result\": {\n", file);
    fputs("    \"state\": ", file);
    df_clean_json_string(file, result->final_state);
    fprintf(file, ",\n    \"disk_number\": %u,\n",
            result->disk_number);
    fprintf(file, "    \"layout_deleted\": %s,\n",
            result->layout_deleted ? "true" : "false");
    fputs("    \"before_style\": ", file);
    df_clean_json_string(file,
                         df_clean_partition_style_name(result->before_style));
    fputs(",\n    \"after_style\": ", file);
    df_clean_json_string(file,
                         df_clean_partition_style_name(result->after_style));
    fprintf(file, ",\n    \"before_partition_count\": %u,\n",
            result->before_partition_count);
    fprintf(file, "    \"after_partition_count\": %u,\n",
            result->after_partition_count);
    fprintf(file, "    \"clean_ms\": %.3f,\n", result->clean_ms);
    fprintf(file, "    \"verify_ms\": %.3f,\n", result->verify_ms);
    fprintf(file, "    \"total_ms\": %.3f\n", result->total_ms);
    fputs("  },\n  \"after_target\": ", file);
    df_clean_write_target_json(file, &result->after_target);
    fputs(",\n  \"error\": {\"code\": ", file);
    df_clean_json_string(file,
                         operation_error != NULL ?
                             df_status_name(operation_error->code) : "ok");
    fprintf(file, ", \"os_code\": %d, \"message\": ",
            operation_error != NULL ? operation_error->os_code : 0);
    df_clean_json_string(file,
                         operation_error != NULL ?
                             operation_error->message : "");
    fputs("}\n}\n", file);

    if (fclose(file) != 0) {
        df_error_set(report_error, DF_ERR_IO, 0,
                     "could not flush clean report '%s'", path);
        return DF_ERR_IO;
    }
    df_error_clear(report_error);
    return DF_OK;
}
