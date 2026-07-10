#ifndef DEADFLASH_WIN32_STUB_WINDOWS_H
#define DEADFLASH_WIN32_STUB_WINDOWS_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HANDLE;
typedef uint32_t DWORD;
typedef int BOOL;
typedef unsigned char BYTE;
typedef int64_t LONGLONG;
typedef uint32_t UINT;
typedef const wchar_t *LPCWSTR;
typedef wchar_t *LPWSTR;
typedef void *LPVOID;
typedef const void *LPCVOID;

typedef union LARGE_INTEGER {
    struct { DWORD LowPart; int32_t HighPart; };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef union ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; };
    uint64_t QuadPart;
} ULARGE_INTEGER;

typedef struct FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define MAX_PATH 260

#define CP_UTF8 65001u
#define MB_ERR_INVALID_CHARS 0x00000008u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define FILE_ATTRIBUTE_READONLY 0x00000001u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_FLAG_WRITE_THROUGH 0x80000000u
#define FILE_FLAG_NO_BUFFERING 0x20000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define FILE_BEGIN 0u
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000u
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200u
#define GetFileExInfoStandard 0

int MultiByteToWideChar(UINT code_page, DWORD flags, const char *source,
                        int source_length, wchar_t *destination,
                        int destination_length);
HANDLE CreateFileW(const wchar_t *path, DWORD access, DWORD share,
                   void *security, DWORD creation, DWORD flags,
                   HANDLE template_file);
BOOL CloseHandle(HANDLE handle);
BOOL DeviceIoControl(HANDLE handle, DWORD code, void *input,
                     DWORD input_size, void *output, DWORD output_size,
                     DWORD *returned, void *overlapped);
DWORD GetLogicalDrives(void);
UINT GetWindowsDirectoryW(wchar_t *buffer, UINT size);
void Sleep(DWORD milliseconds);
DWORD GetLastError(void);
void SetLastError(DWORD code);
DWORD FormatMessageA(DWORD flags, const void *source, DWORD message_id,
                     DWORD language_id, char *buffer, DWORD size,
                     void *arguments);
BOOL GetFileAttributesExW(const wchar_t *path, int level,
                          WIN32_FILE_ATTRIBUTE_DATA *data);
BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER *size);
BOOL SetFilePointerEx(HANDLE handle, LARGE_INTEGER distance,
                      LARGE_INTEGER *new_position, DWORD method);
BOOL SetEndOfFile(HANDLE handle);
BOOL ReadFile(HANDLE handle, void *buffer, DWORD length,
              DWORD *transferred, void *overlapped);
BOOL WriteFile(HANDLE handle, const void *buffer, DWORD length,
               DWORD *transferred, void *overlapped);
BOOL FlushFileBuffers(HANDLE handle);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *frequency);
BOOL QueryPerformanceCounter(LARGE_INTEGER *counter);
void GetSystemTimeAsFileTime(FILETIME *filetime);
int _strnicmp(const char *left, const char *right, size_t count);

#ifdef __cplusplus
}
#endif

#endif
