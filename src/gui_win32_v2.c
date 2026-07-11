#include "deadflash/report.h"

#if !defined(_WIN32)
#error "deadflash-gui is Windows-only"
#endif

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define DF_GUI_CLASS L"DeadflashWindowV2"
#define DF_GUI_TITLE L"DEADFLASH 1.0.0"
#define DF_GUI_MAX_TARGETS 32u
#define DF_GUI_WM_SCAN_DONE (WM_APP + 1u)
#define DF_GUI_WM_WRITE_DONE (WM_APP + 2u)
#define DF_GUI_TIMER_AUTOSCAN 1u

#define IDC_SOURCE 1001
#define IDC_BROWSE 1002
#define IDC_TARGET 1003
#define IDC_REFRESH 1004
#define IDC_DETAILS 1005
#define IDC_VERIFY 1006
#define IDC_DIRECT 1007
#define IDC_DISMOUNT 1008
#define IDC_WRITE 1009
#define IDC_PROGRESS 1010
#define IDC_LOG 1011
#define IDC_ADMIN 1012
#define IDC_ELEVATE 1013

typedef struct df_gui_scan_job {
    HWND owner;
    df_target_info targets[DF_GUI_MAX_TARGETS];
    unsigned count;
} df_gui_scan_job;

typedef struct df_gui_write_job {
    HWND owner;
    char source_path[DF_MAX_PATH_CHARS];
    char report_path[DF_MAX_PATH_CHARS];
    char confirmation_token[DF_TOKEN_HEX_CHARS + 1u];
    df_target_info target;
    df_write_options options;
    df_write_result result;
    df_error error;
    df_error report_error;
    df_status status;
    bool report_written;
} df_gui_write_job;

typedef struct df_gui_state {
    HWND window;
    HWND source;
    HWND browse;
    HWND target;
    HWND refresh;
    HWND details;
    HWND verify;
    HWND direct_io;
    HWND dismount;
    HWND write_button;
    HWND progress;
    HWND log;
    HWND admin;
    HWND elevate;
    HFONT font;
    HFONT mono;
    df_target_info targets[DF_GUI_MAX_TARGETS];
    unsigned target_count;
    bool elevated;
    bool scanning;
    bool writing;
    df_gui_scan_job scan_job;
    df_gui_write_job write_job;
} df_gui_state;

static df_gui_state g_gui;

static void df_gui_startup_trace(const wchar_t *stage, DWORD code) {
    wchar_t temp[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t line[512];
    HANDLE file;
    DWORD written = 0;
    int count;
    if (GetTempPathW(MAX_PATH, temp) == 0u) return;
    if (swprintf_s(path, MAX_PATH, L"%lsdeadflash-gui-startup.log", temp) < 0) return;
    file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    count = swprintf_s(line, sizeof(line) / sizeof(line[0]),
                       L"stage=%ls code=%lu\r\n", stage, (unsigned long)code);
    if (count > 0) {
        (void)WriteFile(file, line, (DWORD)((size_t)count * sizeof(wchar_t)), &written, NULL);
    }
    CloseHandle(file);
}

static void df_gui_show_startup_error(const wchar_t *stage, DWORD code) {
    wchar_t system_message[512];
    wchar_t message[1024];
    DWORD length;
    system_message[0] = L'\0';
    length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, code, 0, system_message,
                            (DWORD)(sizeof(system_message) / sizeof(system_message[0])), NULL);
    if (length == 0u) {
        (void)wcscpy_s(system_message, sizeof(system_message) / sizeof(system_message[0]),
                       L"No Windows error text was available.");
    }
    (void)swprintf_s(message, sizeof(message) / sizeof(message[0]),
                     L"DEADFLASH GUI failed during %ls.\r\n\r\n"
                     L"Windows error: %lu\r\n%ls\r\n\r\n"
                     L"Startup log: %%TEMP%%\\deadflash-gui-startup.log",
                     stage, (unsigned long)code, system_message);
    df_gui_startup_trace(stage, code);
    MessageBoxW(NULL, message, DF_GUI_TITLE, MB_OK | MB_ICONERROR | MB_TOPMOST);
}

static bool df_gui_utf8_to_wide(const char *input, wchar_t *output, size_t output_count) {
    int result;
    if (input == NULL || output == NULL || output_count == 0u || output_count > INT_MAX) return false;
    output[0] = L'\0';
    result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1,
                                 output, (int)output_count);
    return result > 0;
}

static bool df_gui_wide_to_utf8(const wchar_t *input, char *output, size_t output_count) {
    int result;
    if (input == NULL || output == NULL || output_count == 0u || output_count > INT_MAX) return false;
    output[0] = '\0';
    result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1,
                                 output, (int)output_count, NULL, NULL);
    return result > 0;
}

static void df_gui_set_font(HWND control, HFONT font) {
    if (control != NULL && font != NULL) {
        (void)SendMessageW(control, WM_SETFONT, (WPARAM)font, (LPARAM)TRUE);
    }
}

static void df_gui_append_log(const wchar_t *text) {
    LRESULT length;
    if (g_gui.log == NULL || text == NULL) return;
    length = SendMessageW(g_gui.log, WM_GETTEXTLENGTH, 0, 0);
    (void)SendMessageW(g_gui.log, EM_SETSEL, (WPARAM)length, (LPARAM)length);
    (void)SendMessageW(g_gui.log, EM_REPLACESEL, (WPARAM)FALSE, (LPARAM)text);
    (void)SendMessageW(g_gui.log, EM_SCROLLCARET, 0, 0);
}

static void df_gui_format_size(uint64_t bytes, wchar_t output[64]) {
    if (bytes >= UINT64_C(1073741824)) {
        (void)swprintf_s(output, 64u, L"%.2f GiB", (double)bytes / 1073741824.0);
    } else {
        (void)swprintf_s(output, 64u, L"%.1f MiB", (double)bytes / 1048576.0);
    }
}

static bool df_gui_is_elevated(void) {
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation;
    DWORD returned = 0;
    bool elevated = false;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    if (GetTokenInformation(token, TokenElevation, &elevation,
                            (DWORD)sizeof(elevation), &returned)) {
        elevated = elevation.TokenIsElevated != 0;
    }
    CloseHandle(token);
    return elevated;
}

static void df_gui_restart_elevated(void) {
    wchar_t executable[MAX_PATH];
    if (GetModuleFileNameW(NULL, executable, MAX_PATH) == 0u) {
        df_gui_show_startup_error(L"resolve executable path", GetLastError());
        return;
    }
    if ((INT_PTR)ShellExecuteW(g_gui.window, L"runas", executable, NULL, NULL,
                               SW_SHOWNORMAL) <= 32) {
        MessageBoxW(g_gui.window, L"Administrator access was not granted.",
                    DF_GUI_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    DestroyWindow(g_gui.window);
}

static HWND df_gui_control(const wchar_t *class_name, const wchar_t *text,
                           DWORD style, DWORD ex_style, int x, int y,
                           int width, int height, int id) {
    return CreateWindowExW(ex_style, class_name, text, style,
                           x, y, width, height, g_gui.window,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}

static bool df_gui_create_controls(void) {
    LOGFONTW logical_font;
    HFONT stock = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND label;
    if (stock == NULL || GetObjectW(stock, (int)sizeof(logical_font), &logical_font) == 0) return false;
    g_gui.font = CreateFontIndirectW(&logical_font);
    logical_font.lfHeight = -15;
    (void)wcscpy_s(logical_font.lfFaceName, LF_FACESIZE, L"Consolas");
    g_gui.mono = CreateFontIndirectW(&logical_font);
    if (g_gui.font == NULL || g_gui.mono == NULL) return false;

    label = df_gui_control(L"STATIC", L"DEADFLASH 1.0.0   |   WRITE THE IMAGE. VERIFY THE TRUTH.",
                           WS_CHILD | WS_VISIBLE, 0, 20, 18, 600, 24, 0);
    df_gui_set_font(label, g_gui.font);
    g_gui.admin = df_gui_control(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                 0, 610, 18, 260, 24, IDC_ADMIN);
    g_gui.elevate = df_gui_control(L"BUTTON", L"RUN AS ADMIN", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   0, 735, 48, 135, 28, IDC_ELEVATE);

    label = df_gui_control(L"STATIC", L"SOURCE IMAGE", WS_CHILD | WS_VISIBLE,
                           0, 20, 88, 180, 20, 0);
    g_gui.source = df_gui_control(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                  WS_EX_CLIENTEDGE, 20, 110, 710, 28, IDC_SOURCE);
    g_gui.browse = df_gui_control(L"BUTTON", L"SELECT IMAGE", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  0, 742, 109, 128, 30, IDC_BROWSE);

    label = df_gui_control(L"STATIC", L"TARGET DEVICE", WS_CHILD | WS_VISIBLE,
                           0, 20, 154, 180, 20, 0);
    g_gui.target = df_gui_control(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                                  WS_EX_CLIENTEDGE, 20, 176, 710, 260, IDC_TARGET);
    g_gui.refresh = df_gui_control(L"BUTTON", L"REFRESH", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   0, 742, 175, 128, 30, IDC_REFRESH);

    label = df_gui_control(L"STATIC", L"DEVICE OVERVIEW", WS_CHILD | WS_VISIBLE,
                           0, 20, 218, 180, 20, 0);
    g_gui.details = df_gui_control(L"EDIT", L"WINDOW READY. DEVICE SCAN HAS NOT STARTED.",
                                   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                   WS_EX_CLIENTEDGE, 20, 240, 850, 120, IDC_DETAILS);

    label = df_gui_control(L"STATIC", L"VERIFY", WS_CHILD | WS_VISIBLE,
                           0, 20, 380, 100, 20, 0);
    g_gui.verify = df_gui_control(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  WS_EX_CLIENTEDGE, 20, 402, 240, 180, IDC_VERIFY);
    g_gui.direct_io = df_gui_control(L"BUTTON", L"DIRECT I/O", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     0, 285, 402, 130, 26, IDC_DIRECT);
    g_gui.dismount = df_gui_control(L"BUTTON", L"LOCK + DISMOUNT VOLUMES",
                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    0, 430, 402, 260, 26, IDC_DISMOUNT);
    g_gui.write_button = df_gui_control(L"BUTTON", L"WRITE + VERIFY",
                                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                        0, 720, 392, 150, 42, IDC_WRITE);
    g_gui.progress = df_gui_control(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                                    0, 20, 448, 850, 18, IDC_PROGRESS);

    label = df_gui_control(L"STATIC", L"OPERATION LOG", WS_CHILD | WS_VISIBLE,
                           0, 20, 482, 180, 20, 0);
    g_gui.log = df_gui_control(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                               ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                               WS_EX_CLIENTEDGE, 20, 504, 850, 138, IDC_LOG);

    if (g_gui.admin == NULL || g_gui.elevate == NULL || g_gui.source == NULL ||
        g_gui.browse == NULL || g_gui.target == NULL || g_gui.refresh == NULL ||
        g_gui.details == NULL || g_gui.verify == NULL || g_gui.direct_io == NULL ||
        g_gui.dismount == NULL || g_gui.write_button == NULL ||
        g_gui.progress == NULL || g_gui.log == NULL) {
        return false;
    }

    df_gui_set_font(g_gui.admin, g_gui.font);
    df_gui_set_font(g_gui.elevate, g_gui.font);
    df_gui_set_font(g_gui.source, g_gui.font);
    df_gui_set_font(g_gui.browse, g_gui.font);
    df_gui_set_font(g_gui.target, g_gui.font);
    df_gui_set_font(g_gui.refresh, g_gui.font);
    df_gui_set_font(g_gui.details, g_gui.mono);
    df_gui_set_font(g_gui.verify, g_gui.font);
    df_gui_set_font(g_gui.direct_io, g_gui.font);
    df_gui_set_font(g_gui.dismount, g_gui.font);
    df_gui_set_font(g_gui.write_button, g_gui.font);
    df_gui_set_font(g_gui.log, g_gui.mono);

    (void)SendMessageW(g_gui.verify, CB_ADDSTRING, 0, (LPARAM)L"FULL READBACK (RECOMMENDED)");
    (void)SendMessageW(g_gui.verify, CB_ADDSTRING, 0, (LPARAM)L"SAMPLED READBACK");
    (void)SendMessageW(g_gui.verify, CB_ADDSTRING, 0, (LPARAM)L"NONE (NOT PROVEN)");
    (void)SendMessageW(g_gui.verify, CB_SETCURSEL, 0, 0);
    (void)SendMessageW(g_gui.dismount, BM_SETCHECK, BST_CHECKED, 0);
    (void)SendMessageW(g_gui.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    g_gui.elevated = df_gui_is_elevated();
    SetWindowTextW(g_gui.admin, g_gui.elevated ? L"ADMINISTRATOR: YES" : L"ADMINISTRATOR: NO");
    ShowWindow(g_gui.elevate, g_gui.elevated ? SW_HIDE : SW_SHOW);
    df_gui_append_log(L"[START] native Win32 frontend created\r\n");
    df_gui_append_log(L"[START] disk I/O is deferred until the window is visible\r\n");
    return true;
}

static int df_gui_selected_target(void) {
    LRESULT selection = SendMessageW(g_gui.target, CB_GETCURSEL, 0, 0);
    LRESULT data;
    if (selection == CB_ERR) return -1;
    data = SendMessageW(g_gui.target, CB_GETITEMDATA, (WPARAM)selection, 0);
    if (data == CB_ERR || data < 0 || (unsigned)data >= g_gui.target_count) return -1;
    return (int)data;
}

static void df_gui_update_write_enabled(void) {
    wchar_t source[DF_MAX_PATH_CHARS];
    const int selected = df_gui_selected_target();
    bool enabled = g_gui.elevated && !g_gui.scanning && !g_gui.writing && selected >= 0;
    (void)GetWindowTextW(g_gui.source, source, (int)(sizeof(source) / sizeof(source[0])));
    if (source[0] == L'\0') enabled = false;
    if (selected >= 0) {
        const df_target_info *target = &g_gui.targets[(unsigned)selected];
        if (target->system_disk || target->read_only || target->size_bytes == 0u) enabled = false;
        if (target->mounted && SendMessageW(g_gui.dismount, BM_GETCHECK, 0, 0) != BST_CHECKED) {
            enabled = false;
        }
    }
    EnableWindow(g_gui.write_button, enabled ? TRUE : FALSE);
}

static void df_gui_show_target(void) {
    const int selected = df_gui_selected_target();
    wchar_t text[2048];
    wchar_t path[DF_MAX_PATH_CHARS];
    wchar_t vendor[128];
    wchar_t product[256];
    wchar_t bus[64];
    wchar_t size[64];
    wchar_t token[64];
    const wchar_t *strength;
    if (selected < 0) {
        SetWindowTextW(g_gui.details, L"NO TARGET SELECTED.");
        df_gui_update_write_enabled();
        return;
    }
    {
        const df_target_info *target = &g_gui.targets[(unsigned)selected];
        (void)df_gui_utf8_to_wide(target->path, path, sizeof(path) / sizeof(path[0]));
        (void)df_gui_utf8_to_wide(target->vendor[0] != '\0' ? target->vendor : "-",
                                  vendor, sizeof(vendor) / sizeof(vendor[0]));
        (void)df_gui_utf8_to_wide(target->product[0] != '\0' ? target->product : "UNKNOWN",
                                  product, sizeof(product) / sizeof(product[0]));
        (void)df_gui_utf8_to_wide(target->bus_type[0] != '\0' ? target->bus_type : "-",
                                  bus, sizeof(bus) / sizeof(bus[0]));
        (void)df_gui_utf8_to_wide(target->token, token, sizeof(token) / sizeof(token[0]));
        df_gui_format_size(target->size_bytes, size);
        strength = target->serial_bound ? L"SERIAL_BOUND" :
                   (target->descriptor_present ? L"DESCRIPTOR_BOUND" : L"GEOMETRY_ONLY");
        (void)swprintf_s(text, sizeof(text) / sizeof(text[0]),
                         L"PATH       %ls\r\nMODEL      %ls %ls\r\nCAPACITY   %ls\r\n"
                         L"BUS        %ls\r\nSECTORS    %u logical / %u physical\r\n"
                         L"IDENTITY   %ls\r\nTOKEN      %ls\r\n"
                         L"STATE      removable=%ls  mounted=%ls  read-only=%ls  system=%ls",
                         path, vendor, product, size, bus,
                         target->logical_sector_size, target->physical_sector_size,
                         strength, token,
                         target->removable ? L"YES" : L"NO",
                         target->mounted ? L"YES" : L"NO",
                         target->read_only ? L"YES" : L"NO",
                         target->system_disk ? L"BLOCKED" : L"NO");
    }
    SetWindowTextW(g_gui.details, text);
    df_gui_update_write_enabled();
}

static DWORD WINAPI df_gui_scan_thread(LPVOID parameter) {
    df_gui_scan_job *job = (df_gui_scan_job *)parameter;
    unsigned disk;
    job->count = 0u;
    for (disk = 0u; disk < DF_GUI_MAX_TARGETS; ++disk) {
        char path[64];
        df_target_info info;
        df_error error;
        (void)snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%u", disk);
        if (df_inspect_target(path, &info, &error) != DF_OK) continue;
        if (info.kind != DF_TARGET_BLOCK_DEVICE || info.size_bytes == 0u) continue;
        if (job->count < DF_GUI_MAX_TARGETS) job->targets[job->count++] = info;
    }
    (void)PostMessageW(job->owner, DF_GUI_WM_SCAN_DONE, 0, 0);
    return 0u;
}

static void df_gui_begin_scan(void) {
    HANDLE thread;
    if (g_gui.scanning || g_gui.writing) return;
    g_gui.scanning = true;
    g_gui.scan_job.owner = g_gui.window;
    g_gui.scan_job.count = 0u;
    EnableWindow(g_gui.refresh, FALSE);
    EnableWindow(g_gui.target, FALSE);
    SetWindowTextW(g_gui.details, L"SCANNING PHYSICAL DRIVES...\r\n\r\nTHE WINDOW IS ALREADY LIVE.");
    df_gui_append_log(L"[SCAN] started on worker thread\r\n");
    thread = CreateThread(NULL, 0u, df_gui_scan_thread, &g_gui.scan_job, 0u, NULL);
    if (thread == NULL) {
        g_gui.scanning = false;
        EnableWindow(g_gui.refresh, TRUE);
        EnableWindow(g_gui.target, TRUE);
        MessageBoxW(g_gui.window, L"Could not start the device scan worker.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(thread);
}

static void df_gui_finish_scan(void) {
    unsigned i;
    int first_safe = -1;
    g_gui.scanning = false;
    (void)SendMessageW(g_gui.target, CB_RESETCONTENT, 0, 0);
    g_gui.target_count = g_gui.scan_job.count;
    for (i = 0u; i < g_gui.target_count; ++i) {
        wchar_t product[256];
        wchar_t size[64];
        wchar_t label[512];
        LRESULT item;
        g_gui.targets[i] = g_gui.scan_job.targets[i];
        (void)df_gui_utf8_to_wide(g_gui.targets[i].product[0] != '\0' ?
                                  g_gui.targets[i].product : "UNKNOWN DEVICE",
                                  product, sizeof(product) / sizeof(product[0]));
        df_gui_format_size(g_gui.targets[i].size_bytes, size);
        (void)swprintf_s(label, sizeof(label) / sizeof(label[0]),
                         L"%ls  |  %ls  |  %ls%ls",
                         g_gui.targets[i].path[0] != '\0' ? L"PHYSICAL DRIVE" : L"DEVICE",
                         product, size,
                         g_gui.targets[i].system_disk ? L"  |  SYSTEM - BLOCKED" : L"");
        item = SendMessageW(g_gui.target, CB_ADDSTRING, 0, (LPARAM)label);
        if (item != CB_ERR && item != CB_ERRSPACE) {
            (void)SendMessageW(g_gui.target, CB_SETITEMDATA, (WPARAM)item, (LPARAM)i);
            if (first_safe < 0 && !g_gui.targets[i].system_disk && !g_gui.targets[i].read_only) {
                first_safe = (int)item;
            }
        }
    }
    EnableWindow(g_gui.refresh, TRUE);
    EnableWindow(g_gui.target, TRUE);
    if (g_gui.target_count == 0u) {
        SetWindowTextW(g_gui.details,
                       L"NO PHYSICAL DRIVE WAS FOUND.\r\n\r\n"
                       L"RUN AS ADMINISTRATOR, CONNECT A USB DEVICE, THEN PRESS REFRESH.");
        df_gui_append_log(L"[SCAN] no physical drives found\r\n");
    } else {
        const int selection = first_safe >= 0 ? first_safe : 0;
        wchar_t line[128];
        (void)SendMessageW(g_gui.target, CB_SETCURSEL, (WPARAM)selection, 0);
        (void)swprintf_s(line, sizeof(line) / sizeof(line[0]),
                         L"[SCAN] %u physical drive(s) found\r\n", g_gui.target_count);
        df_gui_append_log(line);
        df_gui_show_target();
    }
    df_gui_update_write_enabled();
}

static void df_gui_browse(void) {
    OPENFILENAMEW dialog;
    wchar_t path[DF_MAX_PATH_CHARS];
    ZeroMemory(&dialog, sizeof(dialog));
    ZeroMemory(path, sizeof(path));
    dialog.lStructSize = (DWORD)sizeof(dialog);
    dialog.hwndOwner = g_gui.window;
    dialog.lpstrFilter = L"Disk images (*.img;*.iso;*.bin)\0*.img;*.iso;*.bin\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Select source image";
    if (GetOpenFileNameW(&dialog)) {
        SetWindowTextW(g_gui.source, path);
        df_gui_append_log(L"[SOURCE] image selected\r\n");
        df_gui_update_write_enabled();
    }
}

static bool df_gui_report_path(char output[DF_MAX_PATH_CHARS]) {
    SYSTEMTIME now;
    wchar_t current[DF_MAX_PATH_CHARS];
    wchar_t path[DF_MAX_PATH_CHARS];
    DWORD length = GetCurrentDirectoryW((DWORD)(sizeof(current) / sizeof(current[0])), current);
    if (length == 0u || length >= (DWORD)(sizeof(current) / sizeof(current[0]))) return false;
    GetLocalTime(&now);
    if (swprintf_s(path, sizeof(path) / sizeof(path[0]),
                    L"%ls\\deadflash-evidence-%04u%02u%02u-%02u%02u%02u.json",
                    current, (unsigned)now.wYear, (unsigned)now.wMonth,
                    (unsigned)now.wDay, (unsigned)now.wHour,
                    (unsigned)now.wMinute, (unsigned)now.wSecond) < 0) return false;
    return df_gui_wide_to_utf8(path, output, DF_MAX_PATH_CHARS);
}

static DWORD WINAPI df_gui_write_thread(LPVOID parameter) {
    df_gui_write_job *job = (df_gui_write_job *)parameter;
    df_report_context context;
    memset(&job->result, 0, sizeof(job->result));
    df_error_clear(&job->error);
    df_error_clear(&job->report_error);
    job->options.confirmation_token = job->confirmation_token;
    job->status = df_write_image(job->source_path, job->target.path,
                                 &job->options, &job->target,
                                 &job->result, &job->error);
    memset(&context, 0, sizeof(context));
    context.operation = "write";
    context.source_path = job->source_path;
    context.target_path = job->target.path;
    context.target = &job->target;
    context.write_options = &job->options;
    context.write_result = &job->result;
    context.status = job->status;
    context.error = &job->error;
    job->report_written =
        df_write_json_report(job->report_path, &context, &job->report_error) == DF_OK;
    (void)PostMessageW(job->owner, DF_GUI_WM_WRITE_DONE, 0, 0);
    return 0u;
}

static void df_gui_begin_write(void) {
    const int selected = df_gui_selected_target();
    wchar_t source[DF_MAX_PATH_CHARS];
    wchar_t confirmation[1024];
    wchar_t product[256];
    wchar_t size[64];
    HANDLE thread;
    int verify;
    if (selected < 0 || g_gui.writing || g_gui.scanning) return;
    (void)GetWindowTextW(g_gui.source, source, (int)(sizeof(source) / sizeof(source[0])));
    if (source[0] == L'\0') return;
    if (!g_gui.elevated) {
        MessageBoxW(g_gui.window, L"Administrator access is required.",
                    DF_GUI_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    memset(&g_gui.write_job, 0, sizeof(g_gui.write_job));
    g_gui.write_job.owner = g_gui.window;
    g_gui.write_job.target = g_gui.targets[(unsigned)selected];
    if (g_gui.write_job.target.system_disk) {
        MessageBoxW(g_gui.window, L"The Windows system disk is blocked.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    if (!df_gui_wide_to_utf8(source, g_gui.write_job.source_path,
                              sizeof(g_gui.write_job.source_path)) ||
        !df_gui_report_path(g_gui.write_job.report_path)) {
        MessageBoxW(g_gui.window, L"Could not prepare source or evidence path.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    (void)strcpy_s(g_gui.write_job.confirmation_token,
                   sizeof(g_gui.write_job.confirmation_token),
                   g_gui.write_job.target.token);
    (void)df_gui_utf8_to_wide(g_gui.write_job.target.product[0] != '\0' ?
                              g_gui.write_job.target.product : "UNKNOWN DEVICE",
                              product, sizeof(product) / sizeof(product[0]));
    df_gui_format_size(g_gui.write_job.target.size_bytes, size);
    (void)swprintf_s(confirmation, sizeof(confirmation) / sizeof(confirmation[0]),
                     L"DESTROY ALL DATA ON THIS TARGET?\r\n\r\n%ls\r\n%ls\r\n\r\n"
                     L"The source will be written, flushed, and verified.\r\n"
                     L"This cannot be undone.", product, size);
    if (MessageBoxW(g_gui.window, confirmation, L"DESTRUCTIVE CONFIRMATION",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    g_gui.write_job.options.buffer_size = DF_DEFAULT_BUFFER_SIZE;
    g_gui.write_job.options.write_retries = 4u;
    g_gui.write_job.options.sample_count = 64u;
    g_gui.write_job.options.allow_device = true;
    g_gui.write_job.options.force_mounted =
        SendMessageW(g_gui.dismount, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_gui.write_job.options.force_system_disk = false;
    g_gui.write_job.options.direct_io =
        SendMessageW(g_gui.direct_io, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_gui.write_job.options.truncate_regular_file = true;
    verify = (int)SendMessageW(g_gui.verify, CB_GETCURSEL, 0, 0);
    g_gui.write_job.options.verify_mode = verify == 1 ? DF_VERIFY_SAMPLE :
                                          (verify == 2 ? DF_VERIFY_NONE : DF_VERIFY_FULL);

    g_gui.writing = true;
    EnableWindow(g_gui.browse, FALSE);
    EnableWindow(g_gui.target, FALSE);
    EnableWindow(g_gui.refresh, FALSE);
    EnableWindow(g_gui.verify, FALSE);
    EnableWindow(g_gui.direct_io, FALSE);
    EnableWindow(g_gui.dismount, FALSE);
    EnableWindow(g_gui.write_button, FALSE);
    (void)SendMessageW(g_gui.progress, PBM_SETMARQUEE, (WPARAM)TRUE, 40);
    df_gui_append_log(L"[WRITE] started on worker thread\r\n");
    thread = CreateThread(NULL, 0u, df_gui_write_thread, &g_gui.write_job, 0u, NULL);
    if (thread == NULL) {
        g_gui.writing = false;
        (void)SendMessageW(g_gui.progress, PBM_SETMARQUEE, (WPARAM)FALSE, 0);
        MessageBoxW(g_gui.window, L"Could not start the write worker.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(thread);
}

static void df_gui_finish_write(void) {
    wchar_t line[2048];
    wchar_t state[96];
    wchar_t status[96];
    wchar_t report[DF_MAX_PATH_CHARS];
    g_gui.writing = false;
    (void)SendMessageW(g_gui.progress, PBM_SETMARQUEE, (WPARAM)FALSE, 0);
    (void)SendMessageW(g_gui.progress, PBM_SETPOS,
                       (WPARAM)(g_gui.write_job.status == DF_OK ? 100 : 0), 0);
    (void)df_gui_utf8_to_wide(g_gui.write_job.result.final_state, state,
                              sizeof(state) / sizeof(state[0]));
    (void)df_gui_utf8_to_wide(df_status_name(g_gui.write_job.status), status,
                              sizeof(status) / sizeof(status[0]));
    (void)swprintf_s(line, sizeof(line) / sizeof(line[0]),
                     L"[RESULT] state=%ls status=%ls written=%llu verified=%llu\r\n"
                     L"[TIMING] hash=%.3f write=%.3f flush=%.3f verify=%.3f total=%.3f ms\r\n",
                     state, status,
                     (unsigned long long)g_gui.write_job.result.bytes_written,
                     (unsigned long long)g_gui.write_job.result.bytes_verified,
                     g_gui.write_job.result.source_hash_ms,
                     g_gui.write_job.result.write_ms,
                     g_gui.write_job.result.flush_ms,
                     g_gui.write_job.result.verify_ms,
                     g_gui.write_job.result.total_ms);
    df_gui_append_log(line);
    if (df_gui_utf8_to_wide(g_gui.write_job.report_path, report,
                            sizeof(report) / sizeof(report[0]))) {
        df_gui_append_log(g_gui.write_job.report_written ? L"[EVIDENCE] " : L"[EVIDENCE ERROR] ");
        df_gui_append_log(report);
        df_gui_append_log(L"\r\n");
    }
    EnableWindow(g_gui.browse, TRUE);
    EnableWindow(g_gui.target, TRUE);
    EnableWindow(g_gui.refresh, TRUE);
    EnableWindow(g_gui.verify, TRUE);
    EnableWindow(g_gui.direct_io, TRUE);
    EnableWindow(g_gui.dismount, TRUE);
    df_gui_update_write_enabled();
    MessageBoxW(g_gui.window,
                g_gui.write_job.status == DF_OK ?
                    L"WRITE COMPLETE. FLUSH AND VERIFICATION PASSED." :
                    L"WRITE FAILED. READ THE LOG AND JSON EVIDENCE.",
                DF_GUI_TITLE, MB_OK |
                (g_gui.write_job.status == DF_OK ? MB_ICONINFORMATION : MB_ICONERROR));
}

static LRESULT CALLBACK df_gui_window_proc(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            g_gui.window = window;
            if (!df_gui_create_controls()) {
                df_gui_startup_trace(L"create controls", GetLastError());
                return -1;
            }
            if (SetTimer(window, DF_GUI_TIMER_AUTOSCAN, 350u, NULL) == 0u) {
                df_gui_append_log(L"[START] automatic scan timer failed; press REFRESH\r\n");
            }
            df_gui_update_write_enabled();
            return 0;
        case WM_TIMER:
            if (wparam == DF_GUI_TIMER_AUTOSCAN) {
                (void)KillTimer(window, DF_GUI_TIMER_AUTOSCAN);
                df_gui_begin_scan();
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_BROWSE:
                    if (HIWORD(wparam) == BN_CLICKED) df_gui_browse();
                    return 0;
                case IDC_REFRESH:
                    if (HIWORD(wparam) == BN_CLICKED) df_gui_begin_scan();
                    return 0;
                case IDC_TARGET:
                    if (HIWORD(wparam) == CBN_SELCHANGE) df_gui_show_target();
                    return 0;
                case IDC_DIRECT:
                case IDC_DISMOUNT:
                    if (HIWORD(wparam) == BN_CLICKED) df_gui_update_write_enabled();
                    return 0;
                case IDC_WRITE:
                    if (HIWORD(wparam) == BN_CLICKED) df_gui_begin_write();
                    return 0;
                case IDC_ELEVATE:
                    if (HIWORD(wparam) == BN_CLICKED) df_gui_restart_elevated();
                    return 0;
                default:
                    break;
            }
            break;
        case DF_GUI_WM_SCAN_DONE:
            df_gui_finish_scan();
            return 0;
        case DF_GUI_WM_WRITE_DONE:
            df_gui_finish_write();
            return 0;
        case WM_CLOSE:
            if (g_gui.writing) {
                MessageBoxW(window, L"A destructive write is still running.",
                            DF_GUI_TITLE, MB_OK | MB_ICONWARNING);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (g_gui.font != NULL) (void)DeleteObject(g_gui.font);
            if (g_gui.mono != NULL) (void)DeleteObject(g_gui.mono);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance,
                    PWSTR command_line, int show_command) {
    INITCOMMONCONTROLSEX controls;
    WNDCLASSEXW window_class;
    HWND window;
    MSG message;
    (void)previous_instance;
    (void)command_line;
    (void)show_command;
    ZeroMemory(&g_gui, sizeof(g_gui));
    df_gui_startup_trace(L"entry", 0u);
    (void)SetProcessDPIAware();

    controls.dwSize = (DWORD)sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    if (!InitCommonControlsEx(&controls)) {
        df_gui_show_startup_error(L"InitCommonControlsEx", GetLastError());
        return 1;
    }

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = (UINT)sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = df_gui_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    window_class.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = DF_GUI_CLASS;
    if (RegisterClassExW(&window_class) == 0u) {
        df_gui_show_startup_error(L"RegisterClassExW", GetLastError());
        return 1;
    }

    window = CreateWindowExW(0, DF_GUI_CLASS, DF_GUI_TITLE,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 910, 690,
                             NULL, NULL, instance, NULL);
    if (window == NULL) {
        df_gui_show_startup_error(L"CreateWindowExW", GetLastError());
        return 1;
    }

    ShowWindow(window, SW_SHOW);
    if (!UpdateWindow(window)) {
        const DWORD code = GetLastError();
        if (code != ERROR_SUCCESS) df_gui_startup_trace(L"UpdateWindow", code);
    }
    SetForegroundWindow(window);
    df_gui_startup_trace(L"window visible", 0u);

    for (;;) {
        const BOOL result = GetMessageW(&message, NULL, 0, 0);
        if (result == 0) break;
        if (result == -1) {
            df_gui_show_startup_error(L"GetMessageW", GetLastError());
            return 1;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
