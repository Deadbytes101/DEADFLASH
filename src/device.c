#include "deadflash/io.h"
#include "deadflash/sha256.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winioctl.h>
#include <wctype.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#endif
#endif

#define DF_SAMPLE_BLOCK_SIZE (1024u * 1024u)
#define DF_DEFAULT_SAMPLE_COUNT 64u
#define DF_DEFAULT_WRITE_RETRIES 4u

static void df_make_token(df_target_info *info) {
    char identity[DF_MAX_PATH_CHARS + 256];
    uint8_t digest[32];
    char hex[65];
    int written;
    written = snprintf(identity, sizeof(identity), "%s|%u|%" PRIu64 "|%u|%u|%u|%u",
                       info->path, (unsigned)info->kind, info->size_bytes,
                       info->logical_sector_size, info->physical_sector_size,
                       info->read_only ? 1u : 0u, info->system_disk ? 1u : 0u);
    if (written < 0) identity[0] = '\0';
    identity[sizeof(identity) - 1] = '\0';
    df_sha256_buffer(identity, strlen(identity), digest);
    df_hex_encode(digest, sizeof(digest), hex);
    memcpy(info->token, hex, DF_TOKEN_HEX_CHARS);
    info->token[DF_TOKEN_HEX_CHARS] = '\0';
}

#if defined(_WIN32)

static wchar_t *df_utf8_to_wide(const char *text, df_error *error) {
    int needed;
    wchar_t *wide;
    if (text == NULL) return NULL;
    needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (needed <= 0) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, (int)GetLastError(), "invalid UTF-8 path");
        return NULL;
    }
    wide = (wchar_t *)calloc((size_t)needed, sizeof(wchar_t));
    if (wide == NULL) {
        df_error_set(error, DF_ERR_NO_MEMORY, 0, "could not allocate path buffer");
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, needed) <= 0) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, (int)GetLastError(), "could not convert UTF-8 path");
        free(wide);
        return NULL;
    }
    return wide;
}

static bool df_is_physical_drive_path(const char *path, unsigned *number) {
    const char *prefix1 = "\\\\.\\PhysicalDrive";
    const char *prefix2 = "//./PhysicalDrive";
    const char *digits = NULL;
    char *end = NULL;
    unsigned long value;
    if (path == NULL) return false;
    if (_strnicmp(path, prefix1, strlen(prefix1)) == 0) digits = path + strlen(prefix1);
    else if (_strnicmp(path, prefix2, strlen(prefix2)) == 0) digits = path + strlen(prefix2);
    else return false;
    if (*digits == '\0') return false;
    value = strtoul(digits, &end, 10);
    if (end == digits || *end != '\0' || value > 255u) return false;
    if (number != NULL) *number = (unsigned)value;
    return true;
}

static df_status df_win_error(df_error *error, df_status status, const char *operation);

static bool df_volume_uses_disk(wchar_t letter, DWORD disk_number) {
    wchar_t path[] = L"\\\\.\\C:";
    HANDLE volume;
    BYTE buffer[sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 15u];
    DWORD returned = 0;
    VOLUME_DISK_EXTENTS *extents = (VOLUME_DISK_EXTENTS *)buffer;
    DWORD i;
    path[4] = letter;
    volume = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    if (volume == INVALID_HANDLE_VALUE) return false;
    if (!DeviceIoControl(volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
                         buffer, (DWORD)sizeof(buffer), &returned, NULL)) {
        CloseHandle(volume);
        return false;
    }
    CloseHandle(volume);
    for (i = 0; i < extents->NumberOfDiskExtents; ++i) {
        if (extents->Extents[i].DiskNumber == disk_number) return true;
    }
    return false;
}

static void df_windows_mount_state(DWORD disk_number, bool *mounted, bool *system_disk) {
    DWORD drives = GetLogicalDrives();
    wchar_t windows_dir[MAX_PATH];
    wchar_t system_letter = 0;
    unsigned i;
    *mounted = false;
    *system_disk = false;
    if (GetWindowsDirectoryW(windows_dir, MAX_PATH) > 1 && windows_dir[1] == L':') {
        system_letter = (wchar_t)towupper(windows_dir[0]);
    }
    for (i = 0; i < 26; ++i) {
        wchar_t letter;
        if ((drives & (1u << i)) == 0) continue;
        letter = (wchar_t)(L'A' + i);
        if (df_volume_uses_disk(letter, disk_number)) {
            *mounted = true;
            if (letter == system_letter) *system_disk = true;
        }
    }
}

static df_status df_windows_lock_disk_volumes(DWORD disk_number, df_file *file, df_error *error) {
    DWORD drives = GetLogicalDrives();
    unsigned i;
    file->locked_volume_count = 0;
    for (i = 0; i < 26; ++i) {
        wchar_t path[] = L"\\\\.\\C:";
        HANDLE volume;
        DWORD returned = 0;
        unsigned retry;
        if ((drives & (1u << i)) == 0) continue;
        if (!df_volume_uses_disk((wchar_t)(L'A' + i), disk_number)) continue;
        path[4] = (wchar_t)(L'A' + i);
        volume = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, 0, NULL);
        if (volume == INVALID_HANDLE_VALUE) {
            return df_win_error(error, DF_ERR_OPEN, "open target volume for lock");
        }
        for (retry = 0; retry < 20; ++retry) {
            if (DeviceIoControl(volume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &returned, NULL)) break;
            Sleep(100);
        }
        if (retry == 20) {
            CloseHandle(volume);
            return df_win_error(error, DF_ERR_MOUNTED, "lock target volume");
        }
        if (!DeviceIoControl(volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &returned, NULL)) {
            (void)DeviceIoControl(volume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &returned, NULL);
            CloseHandle(volume);
            return df_win_error(error, DF_ERR_MOUNTED, "dismount target volume");
        }
        file->locked_volumes[file->locked_volume_count++] = volume;
    }
    return DF_OK;
}

static void df_windows_unlock_volumes(df_file *file) {
    unsigned i;
    DWORD returned = 0;
    if (file == NULL) return;
    for (i = 0; i < file->locked_volume_count; ++i) {
        if (file->locked_volumes[i] != INVALID_HANDLE_VALUE) {
            (void)DeviceIoControl(file->locked_volumes[i], FSCTL_UNLOCK_VOLUME,
                                  NULL, 0, NULL, 0, &returned, NULL);
            CloseHandle(file->locked_volumes[i]);
            file->locked_volumes[i] = INVALID_HANDLE_VALUE;
        }
    }
    file->locked_volume_count = 0;
}

static df_status df_win_error(df_error *error, df_status status, const char *operation) {
    DWORD code = GetLastError();
    char message[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, code, 0, message, (DWORD)sizeof(message), NULL);
    while (message[0] != '\0') {
        size_t n = strlen(message);
        if (n == 0 || (message[n - 1] != '\r' && message[n - 1] != '\n')) break;
        message[n - 1] = '\0';
    }
    df_error_set(error, status, (int)code, "%s: %s", operation, message[0] ? message : "Windows error");
    return status;
}

#else

static df_status df_errno_error(df_error *error, df_status status, const char *operation, const char *path) {
    int code = errno;
    df_error_set(error, status, code, "%s '%s': %s", operation,
                 path != NULL ? path : "", strerror(code));
    return status;
}

#if defined(__linux__)
static bool df_sysfs_whole_disk_name(unsigned major_no, unsigned minor_no, char *name, size_t name_size) {
    char link_path[128];
    char resolved[PATH_MAX];
    char *block;
    char *slash;
    int n;
    n = snprintf(link_path, sizeof(link_path), "/sys/dev/block/%u:%u", major_no, minor_no);
    if (n < 0 || (size_t)n >= sizeof(link_path)) return false;
    if (realpath(link_path, resolved) == NULL) return false;
    block = strstr(resolved, "/block/");
    if (block == NULL) return false;
    block += strlen("/block/");
    slash = strchr(block, '/');
    if (slash != NULL) *slash = '\0';
    if (*block == '\0' || strlen(block) + 1 > name_size) return false;
    memcpy(name, block, strlen(block) + 1);
    return true;
}

static void df_linux_mount_state(dev_t target_dev, bool *mounted, bool *system_disk) {
    FILE *file;
    char line[8192];
    char target_disk[256] = {0};
    *mounted = false;
    *system_disk = false;
    if (!df_sysfs_whole_disk_name(major(target_dev), minor(target_dev), target_disk, sizeof(target_disk))) return;
    file = fopen("/proc/self/mountinfo", "r");
    if (file == NULL) return;
    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned mount_major = 0, mount_minor = 0;
        unsigned long mount_id = 0, parent_id = 0;
        char mount_point[PATH_MAX] = {0};
        char mount_disk[256] = {0};
        int fields;
        fields = sscanf(line, "%lu %lu %u:%u %*s %4095s", &mount_id, &parent_id,
                        &mount_major, &mount_minor, mount_point);
        if (fields != 5) continue;
        if (!df_sysfs_whole_disk_name(mount_major, mount_minor, mount_disk, sizeof(mount_disk))) continue;
        if (strcmp(target_disk, mount_disk) == 0) {
            *mounted = true;
            if (strcmp(mount_point, "/") == 0 || strcmp(mount_point, "/boot") == 0 ||
                strcmp(mount_point, "/boot/efi") == 0) {
                *system_disk = true;
            }
        }
    }
    fclose(file);
}
#endif

#endif

static void df_target_info_defaults(df_target_info *info, const char *path) {
    memset(info, 0, sizeof(*info));
    if (path != NULL) {
        (void)snprintf(info->path, sizeof(info->path), "%s", path);
        (void)snprintf(info->display_name, sizeof(info->display_name), "%s", df_path_basename(path));
    }
    info->kind = DF_TARGET_UNKNOWN;
    info->logical_sector_size = 512;
    info->physical_sector_size = 512;
}

df_status df_inspect_target(const char *path, df_target_info *info, df_error *error) {
    if (path == NULL || info == NULL || *path == '\0') {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "target path is required");
        return DF_ERR_INVALID_ARGUMENT;
    }
    df_error_clear(error);
    df_target_info_defaults(info, path);

#if defined(_WIN32)
    {
        unsigned disk_number = 0;
        wchar_t *wide = NULL;
        HANDLE handle = INVALID_HANDLE_VALUE;
        if (df_is_physical_drive_path(path, &disk_number)) {
            GET_LENGTH_INFORMATION length_info;
            STORAGE_HOTPLUG_INFO hotplug;
            DISK_GEOMETRY_EX geometry;
            DWORD returned = 0;
            DWORD writable_returned = 0;
            wide = df_utf8_to_wide(path, error);
            if (wide == NULL) return error != NULL ? error->code : DF_ERR_INVALID_ARGUMENT;
            handle = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, 0, NULL);
            free(wide);
            if (handle == INVALID_HANDLE_VALUE) return df_win_error(error, DF_ERR_OPEN, "open target for inspection");
            info->kind = DF_TARGET_BLOCK_DEVICE;
            if (DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &length_info,
                                (DWORD)sizeof(length_info), &returned, NULL)) {
                info->size_bytes = (uint64_t)length_info.Length.QuadPart;
            } else if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                                       &geometry, (DWORD)sizeof(geometry), &returned, NULL)) {
                info->size_bytes = (uint64_t)geometry.DiskSize.QuadPart;
                info->logical_sector_size = geometry.Geometry.BytesPerSector;
            } else {
                CloseHandle(handle);
                return df_win_error(error, DF_ERR_IO, "query target size");
            }
            memset(&geometry, 0, sizeof(geometry));
            if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                                &geometry, (DWORD)sizeof(geometry), &returned, NULL)) {
                info->logical_sector_size = geometry.Geometry.BytesPerSector;
                info->physical_sector_size = geometry.Geometry.BytesPerSector;
            }
            memset(&hotplug, 0, sizeof(hotplug));
            if (DeviceIoControl(handle, IOCTL_STORAGE_GET_HOTPLUG_INFO, NULL, 0,
                                &hotplug, (DWORD)sizeof(hotplug), &returned, NULL)) {
                info->removable = hotplug.MediaRemovable || hotplug.DeviceHotplug;
            }
            info->read_only = !DeviceIoControl(handle, IOCTL_DISK_IS_WRITABLE, NULL, 0,
                                                NULL, 0, &writable_returned, NULL);
            df_windows_mount_state((DWORD)disk_number, &info->mounted, &info->system_disk);
            CloseHandle(handle);
        } else {
            WIN32_FILE_ATTRIBUTE_DATA attributes;
            wide = df_utf8_to_wide(path, error);
            if (wide == NULL) return error != NULL ? error->code : DF_ERR_INVALID_ARGUMENT;
            if (!GetFileAttributesExW(wide, GetFileExInfoStandard, &attributes)) {
                DWORD code = GetLastError();
                free(wide);
                if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
                    info->kind = DF_TARGET_REGULAR_FILE;
                    info->size_bytes = 0;
                    df_make_token(info);
                    return DF_OK;
                }
                SetLastError(code);
                return df_win_error(error, DF_ERR_OPEN, "inspect target");
            }
            free(wide);
            if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "target is a directory");
                return DF_ERR_INVALID_ARGUMENT;
            }
            info->kind = DF_TARGET_REGULAR_FILE;
            info->size_bytes = ((uint64_t)attributes.nFileSizeHigh << 32) | attributes.nFileSizeLow;
            info->read_only = (attributes.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        }
    }
#else
    {
        struct stat st;
        if (stat(path, &st) != 0) {
            if (errno == ENOENT) {
                info->kind = DF_TARGET_REGULAR_FILE;
                info->size_bytes = 0;
                df_make_token(info);
                return DF_OK;
            }
            return df_errno_error(error, DF_ERR_OPEN, "inspect target", path);
        }
        if (S_ISREG(st.st_mode)) {
            info->kind = DF_TARGET_REGULAR_FILE;
            info->size_bytes = (uint64_t)st.st_size;
            info->read_only = access(path, W_OK) != 0;
        } else if (S_ISBLK(st.st_mode)) {
            info->kind = DF_TARGET_BLOCK_DEVICE;
#if defined(__linux__)
            {
                int fd = open(path, O_RDONLY | O_CLOEXEC);
                unsigned int logical = 512, physical = 512;
                int read_only = 0;
                uint64_t bytes = 0;
                if (fd < 0) return df_errno_error(error, DF_ERR_OPEN, "open block device", path);
                if (ioctl(fd, BLKGETSIZE64, &bytes) != 0) {
                    close(fd);
                    return df_errno_error(error, DF_ERR_IO, "query block device size", path);
                }
                (void)ioctl(fd, BLKSSZGET, &logical);
#ifdef BLKPBSZGET
                (void)ioctl(fd, BLKPBSZGET, &physical);
#endif
                (void)ioctl(fd, BLKROGET, &read_only);
                close(fd);
                info->size_bytes = bytes;
                info->logical_sector_size = logical != 0 ? logical : 512;
                info->physical_sector_size = physical != 0 ? physical : info->logical_sector_size;
                info->read_only = read_only != 0;
                info->removable = false;
                df_linux_mount_state(st.st_rdev, &info->mounted, &info->system_disk);
                {
                    char removable_path[PATH_MAX];
                    char disk[256];
                    FILE *removable_file;
                    int removable_value = 0;
                    if (df_sysfs_whole_disk_name(major(st.st_rdev), minor(st.st_rdev), disk, sizeof(disk))) {
                        (void)snprintf(removable_path, sizeof(removable_path), "/sys/class/block/%s/removable", disk);
                        removable_file = fopen(removable_path, "r");
                        if (removable_file != NULL) {
                            if (fscanf(removable_file, "%d", &removable_value) == 1) info->removable = removable_value != 0;
                            fclose(removable_file);
                        }
                    }
                }
            }
#else
            info->size_bytes = 0;
#endif
        } else if (S_ISCHR(st.st_mode)) {
            info->kind = DF_TARGET_CHAR_DEVICE;
            info->size_bytes = 0;
        } else {
            df_error_set(error, DF_ERR_UNSUPPORTED, 0, "unsupported target type: %s", path);
            return DF_ERR_UNSUPPORTED;
        }
    }
#endif
    df_make_token(info);
    return DF_OK;
}

df_status df_list_targets(FILE *output, df_error *error) {
    if (output == NULL) output = stdout;
    df_error_clear(error);
#if defined(_WIN32)
    {
        unsigned i;
        for (i = 0; i < 32; ++i) {
            char path[64];
            df_target_info info;
            (void)snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%u", i);
            if (df_inspect_target(path, &info, NULL) == DF_OK) {
                fprintf(output, "%s\t%" PRIu64 "\tsector=%u\tremovable=%s\tmounted=%s\tsystem=%s\ttoken=%s\n",
                        path, info.size_bytes, info.logical_sector_size,
                        info.removable ? "yes" : "no", info.mounted ? "yes" : "no",
                        info.system_disk ? "yes" : "no", info.token);
            }
        }
    }
#elif defined(__linux__)
    {
        DIR *dir = opendir("/sys/class/block");
        struct dirent *entry;
        if (dir == NULL) return df_errno_error(error, DF_ERR_OPEN, "open", "/sys/class/block");
        while ((entry = readdir(dir)) != NULL) {
            char path[PATH_MAX];
            df_target_info info;
            if (entry->d_name[0] == '.') continue;
            if (snprintf(path, sizeof(path), "/dev/%s", entry->d_name) < 0) continue;
            if (df_inspect_target(path, &info, NULL) == DF_OK && info.kind == DF_TARGET_BLOCK_DEVICE) {
                fprintf(output, "%s\t%" PRIu64 "\tsector=%u\tremovable=%s\tmounted=%s\tsystem=%s\ttoken=%s\n",
                        path, info.size_bytes, info.logical_sector_size,
                        info.removable ? "yes" : "no", info.mounted ? "yes" : "no",
                        info.system_disk ? "yes" : "no", info.token);
            }
        }
        closedir(dir);
    }
#else
    df_error_set(error, DF_ERR_UNSUPPORTED, 0, "device listing is not implemented on this platform");
    return DF_ERR_UNSUPPORTED;
#endif
    return DF_OK;
}

static void df_file_reset(df_file *file) {
    if (file == NULL) return;
    memset(file, 0, sizeof(*file));
    file->handle = DF_INVALID_HANDLE;
    file->alignment = 512;
#if defined(_WIN32)
    {
        unsigned i;
        for (i = 0; i < 26; ++i) file->locked_volumes[i] = INVALID_HANDLE_VALUE;
        file->locked_volume_count = 0;
    }
#endif
}

df_status df_open_source(const char *path, df_file *file, df_error *error) {
    if (path == NULL || file == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "source path is required");
        return DF_ERR_INVALID_ARGUMENT;
    }
    df_file_reset(file);
    (void)snprintf(file->path, sizeof(file->path), "%s", path);
#if defined(_WIN32)
    {
        wchar_t *wide = df_utf8_to_wide(path, error);
        LARGE_INTEGER size;
        unsigned disk_number = 0;
        bool physical_drive;
        DWORD flags;
        if (wide == NULL) return error != NULL ? error->code : DF_ERR_INVALID_ARGUMENT;
        physical_drive = df_is_physical_drive_path(path, &disk_number);
        flags = physical_drive ? FILE_ATTRIBUTE_NORMAL :
                (FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN);
        file->handle = CreateFileW(wide, GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, flags, NULL);
        free(wide);
        if (file->handle == INVALID_HANDLE_VALUE) return df_win_error(error, DF_ERR_OPEN, "open source");
        if (physical_drive) {
            GET_LENGTH_INFORMATION length_info;
            DWORD returned = 0;
            if (!DeviceIoControl(file->handle, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                                 &length_info, (DWORD)sizeof(length_info), &returned, NULL)) {
                df_close_file(file);
                return df_win_error(error, DF_ERR_IO, "query physical source size");
            }
            file->size_bytes = (uint64_t)length_info.Length.QuadPart;
            file->is_device = true;
        } else {
            if (!GetFileSizeEx(file->handle, &size)) {
                df_close_file(file);
                return df_win_error(error, DF_ERR_IO, "query source size");
            }
            file->size_bytes = (uint64_t)size.QuadPart;
        }
    }
#else
    {
        struct stat st;
        file->handle = open(path, O_RDONLY | O_CLOEXEC);
        if (file->handle < 0) return df_errno_error(error, DF_ERR_OPEN, "open source", path);
        if (fstat(file->handle, &st) != 0) {
            df_close_file(file);
            return df_errno_error(error, DF_ERR_IO, "stat source", path);
        }
        if (!S_ISREG(st.st_mode) && !S_ISBLK(st.st_mode)) {
            df_close_file(file);
            df_error_set(error, DF_ERR_UNSUPPORTED, 0, "source must be a regular file or block device");
            return DF_ERR_UNSUPPORTED;
        }
        if (S_ISREG(st.st_mode)) file->size_bytes = (uint64_t)st.st_size;
#if defined(__linux__)
        else if (ioctl(file->handle, BLKGETSIZE64, &file->size_bytes) != 0) {
            df_close_file(file);
            return df_errno_error(error, DF_ERR_IO, "query source size", path);
        }
#endif
        file->is_device = S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode);
    }
#endif
    file->writable = false;
    return DF_OK;
}

static df_status df_validate_destructive_target(const df_target_info *info,
                                                 const df_write_options *options,
                                                 df_error *error) {
    if (info->kind == DF_TARGET_BLOCK_DEVICE || info->kind == DF_TARGET_CHAR_DEVICE) {
        if (!options->allow_device) {
            df_error_set(error, DF_ERR_DEVICE_FORBIDDEN, 0,
                         "physical device writes require --allow-device");
            return DF_ERR_DEVICE_FORBIDDEN;
        }
        if (info->read_only) {
            df_error_set(error, DF_ERR_PERMISSION, 0, "target device is read-only");
            return DF_ERR_PERMISSION;
        }
        if (info->system_disk && !options->force_system_disk) {
            df_error_set(error, DF_ERR_SYSTEM_DISK, 0,
                         "target belongs to the running system disk; operation rejected");
            return DF_ERR_SYSTEM_DISK;
        }
#if !defined(_WIN32)
        if (info->mounted && !options->force_mounted) {
            df_error_set(error, DF_ERR_MOUNTED, 0,
                         "target or one of its partitions is mounted; unmount it first");
            return DF_ERR_MOUNTED;
        }
#endif
        if (options->confirmation_token == NULL ||
            strcmp(options->confirmation_token, info->token) != 0) {
            df_error_set(error, DF_ERR_CONFIRMATION, 0,
                         "device confirmation token mismatch; expected %s", info->token);
            return DF_ERR_CONFIRMATION;
        }
    }
    return DF_OK;
}

df_status df_open_target(const char *path, const df_target_info *expected,
                         const df_write_options *options, df_file *file,
                         df_error *error) {
    df_target_info current;
    df_status status;
    if (path == NULL || expected == NULL || options == NULL || file == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "invalid target open arguments");
        return DF_ERR_INVALID_ARGUMENT;
    }
    status = df_inspect_target(path, &current, error);
    if (status != DF_OK) return status;
    if (strcmp(current.token, expected->token) != 0) {
        df_error_set(error, DF_ERR_IDENTITY_CHANGED, 0,
                     "target identity changed between planning and execution");
        return DF_ERR_IDENTITY_CHANGED;
    }
    status = df_validate_destructive_target(&current, options, error);
    if (status != DF_OK) return status;

    df_file_reset(file);
    (void)snprintf(file->path, sizeof(file->path), "%s", path);
    file->size_bytes = current.size_bytes;
    file->alignment = current.logical_sector_size != 0 ? current.logical_sector_size : 512;
    file->is_device = current.kind != DF_TARGET_REGULAR_FILE;
    file->writable = true;
    file->direct_io = options->direct_io && file->is_device;

#if defined(_WIN32)
    {
        wchar_t *wide = df_utf8_to_wide(path, error);
        DWORD creation = file->is_device ? OPEN_EXISTING : OPEN_ALWAYS;
        DWORD flags = FILE_ATTRIBUTE_NORMAL;
        if (file->is_device && options->direct_io) flags |= FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;
        if (wide == NULL) return error != NULL ? error->code : DF_ERR_INVALID_ARGUMENT;
        file->handle = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE,
                                   file->is_device ? (FILE_SHARE_READ | FILE_SHARE_WRITE) : FILE_SHARE_READ,
                                   NULL, creation, flags, NULL);
        free(wide);
        if (file->handle == INVALID_HANDLE_VALUE) return df_win_error(error, DF_ERR_OPEN, "open target");
        if (file->is_device) {
            unsigned disk_number = 0;
            if (!df_is_physical_drive_path(path, &disk_number)) {
                df_close_file(file);
                df_error_set(error, DF_ERR_UNSUPPORTED, 0, "unsupported Windows device path");
                return DF_ERR_UNSUPPORTED;
            }
            status = df_windows_lock_disk_volumes((DWORD)disk_number, file, error);
            if (status != DF_OK) {
                df_close_file(file);
                return status;
            }
        }
    }
#else
    {
        int flags = O_RDWR | O_CLOEXEC;
#ifdef O_DIRECT
        if (file->direct_io) flags |= O_DIRECT;
#endif
        if (!file->is_device) flags |= O_CREAT;
        file->handle = open(path, flags, 0644);
        if (file->handle < 0 && file->direct_io && errno == EINVAL) {
#ifdef O_DIRECT
            flags &= ~O_DIRECT;
#endif
            file->direct_io = false;
            file->handle = open(path, flags, 0644);
        }
        if (file->handle < 0) return df_errno_error(error, DF_ERR_OPEN, "open target", path);
    }
#endif
    return DF_OK;
}

void df_close_file(df_file *file) {
    if (file == NULL || file->handle == DF_INVALID_HANDLE) return;
#if defined(_WIN32)
    df_windows_unlock_volumes(file);
    CloseHandle(file->handle);
#else
    close(file->handle);
#endif
    file->handle = DF_INVALID_HANDLE;
}

df_status df_read_at(df_file *file, void *buffer, size_t length, uint64_t offset,
                     size_t *bytes_read, df_error *error) {
    if (bytes_read != NULL) *bytes_read = 0;
    if (file == NULL || file->handle == DF_INVALID_HANDLE || buffer == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "invalid read arguments");
        return DF_ERR_INVALID_ARGUMENT;
    }
#if defined(_WIN32)
    {
        LARGE_INTEGER position;
        DWORD transferred = 0;
        if (length > 0xffffffffu) {
            df_error_set(error, DF_ERR_TOO_LARGE, 0, "single read exceeds Windows DWORD length");
            return DF_ERR_TOO_LARGE;
        }
        position.QuadPart = (LONGLONG)offset;
        if (!SetFilePointerEx(file->handle, position, NULL, FILE_BEGIN))
            return df_win_error(error, DF_ERR_IO, "seek for read");
        if (!ReadFile(file->handle, buffer, (DWORD)length, &transferred, NULL))
            return df_win_error(error, DF_ERR_IO, "read");
        if (bytes_read != NULL) *bytes_read = (size_t)transferred;
    }
#else
    {
        ssize_t result;
        do {
            result = pread(file->handle, buffer, length, (off_t)offset);
        } while (result < 0 && errno == EINTR);
        if (result < 0) return df_errno_error(error, DF_ERR_IO, "read", file->path);
        if (bytes_read != NULL) *bytes_read = (size_t)result;
    }
#endif
    return DF_OK;
}

df_status df_write_at(df_file *file, const void *buffer, size_t length, uint64_t offset,
                      size_t *bytes_written, df_error *error) {
    if (bytes_written != NULL) *bytes_written = 0;
    if (file == NULL || file->handle == DF_INVALID_HANDLE || buffer == NULL || !file->writable) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "invalid write arguments");
        return DF_ERR_INVALID_ARGUMENT;
    }
#if defined(_WIN32)
    {
        LARGE_INTEGER position;
        DWORD transferred = 0;
        if (length > 0xffffffffu) {
            df_error_set(error, DF_ERR_TOO_LARGE, 0, "single write exceeds Windows DWORD length");
            return DF_ERR_TOO_LARGE;
        }
        position.QuadPart = (LONGLONG)offset;
        if (!SetFilePointerEx(file->handle, position, NULL, FILE_BEGIN))
            return df_win_error(error, DF_ERR_IO, "seek for write");
        if (!WriteFile(file->handle, buffer, (DWORD)length, &transferred, NULL))
            return df_win_error(error, DF_ERR_IO, "write");
        if (bytes_written != NULL) *bytes_written = (size_t)transferred;
    }
#else
    {
        ssize_t result;
        do {
            result = pwrite(file->handle, buffer, length, (off_t)offset);
        } while (result < 0 && errno == EINTR);
        if (result < 0) return df_errno_error(error, DF_ERR_IO, "write", file->path);
        if (bytes_written != NULL) *bytes_written = (size_t)result;
    }
#endif
    return DF_OK;
}

df_status df_flush(df_file *file, df_error *error) {
    if (file == NULL || file->handle == DF_INVALID_HANDLE) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "invalid flush target");
        return DF_ERR_INVALID_ARGUMENT;
    }
#if defined(_WIN32)
    if (!FlushFileBuffers(file->handle)) return df_win_error(error, DF_ERR_IO, "flush target");
#else
    if (fsync(file->handle) != 0) return df_errno_error(error, DF_ERR_IO, "flush target", file->path);
#endif
    return DF_OK;
}

df_status df_resize_regular_file(df_file *file, uint64_t size, df_error *error) {
    if (file == NULL || file->handle == DF_INVALID_HANDLE || file->is_device) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "resize requires a regular target file");
        return DF_ERR_INVALID_ARGUMENT;
    }
#if defined(_WIN32)
    {
        LARGE_INTEGER position;
        position.QuadPart = (LONGLONG)size;
        if (!SetFilePointerEx(file->handle, position, NULL, FILE_BEGIN) || !SetEndOfFile(file->handle)) {
            return df_win_error(error, DF_ERR_IO, "resize target");
        }
    }
#else
    if (ftruncate(file->handle, (off_t)size) != 0) return df_errno_error(error, DF_ERR_IO, "resize target", file->path);
#endif
    file->size_bytes = size;
    return DF_OK;
}
