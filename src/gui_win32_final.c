#include "deadflash/attest.h"
#include "deadflash/report.h"

#if !defined(_WIN32)
#error "deadflash-gui is Windows-only"
#endif

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef PBM_SETSTATE
#define PBM_SETSTATE (WM_USER + 16)
#define PBST_NORMAL 0x0001
#define PBST_ERROR 0x0002
#define PBST_PAUSED 0x0003
#endif

#define DF_GUI_CLASS L"DeadflashFinalWindow"
#define DF_GUI_TITLE L"DEADFLASH 1.0.0"
#define DF_GUI_MAX_TARGETS 32u

#define DF_GUI_WM_SCAN_DONE (WM_APP + 1u)
#define DF_GUI_WM_SOURCE_DONE (WM_APP + 2u)
#define DF_GUI_WM_PLAN_DONE (WM_APP + 3u)
#define DF_GUI_WM_WRITE_DONE (WM_APP + 4u)
#define DF_GUI_WM_PROGRESS (WM_APP + 5u)
#define DF_GUI_TIMER_AUTOSCAN 1u

#define IDC_SOURCE 1001
#define IDC_BROWSE 1002
#define IDC_SOURCE_INFO 1003
#define IDC_TARGET 1004
#define IDC_REFRESH 1005
#define IDC_DETAILS 1006
#define IDC_VERIFY 1007
#define IDC_DIRECT 1008
#define IDC_DISMOUNT 1009
#define IDC_WRITE 1010
#define IDC_PROGRESS 1011
#define IDC_PROGRESS_TEXT 1012
#define IDC_LOG 1013
#define IDC_ADMIN 1014
#define IDC_ELEVATE 1015
#define IDC_STATUS 1016
#define IDC_EVIDENCE 1017

typedef enum df_gui_status_kind {
    DF_GUI_STATUS_IDLE = 0,
    DF_GUI_STATUS_BUSY,
    DF_GUI_STATUS_READY,
    DF_GUI_STATUS_WARNING,
    DF_GUI_STATUS_ERROR,
    DF_GUI_STATUS_COMPLETE
} df_gui_status_kind;

typedef struct df_gui_progress_sink {
    HWND owner;
} df_gui_progress_sink;

typedef struct df_gui_progress_event {
    df_progress_phase phase;
    uint64_t completed;
    uint64_t total;
} df_gui_progress_event;

typedef struct df_gui_scan_job {
    HWND owner;
    df_target_info targets[DF_GUI_MAX_TARGETS];
    unsigned count;
    unsigned blocked_system_count;
} df_gui_scan_job;

typedef struct df_gui_source_job {
    HWND owner;
    char path[DF_MAX_PATH_CHARS];
    uint64_t size_bytes;
    uint8_t sha256[32];
    df_status status;
    df_error error;
    df_gui_progress_sink progress;
} df_gui_source_job;

typedef struct df_gui_operation_job {
    HWND owner;
    char source_path[DF_MAX_PATH_CHARS];
    char report_path[DF_MAX_PATH_CHARS];
    char confirmation_token[DF_TOKEN_HEX_CHARS + 1u];
    df_target_info target;
    df_write_options options;
    df_plan_attestation attestation;
    df_write_result result;
    df_error error;
    df_error report_error;
    df_status status;
    bool report_written;
    df_gui_progress_sink progress;
} df_gui_operation_job;

typedef struct df_gui_state {
    HWND window;
    HWND header;
    HWND admin;
    HWND elevate;
    HWND status;
    HWND source_label;
    HWND source;
    HWND browse;
    HWND source_info;
    HWND target_label;
    HWND target;
    HWND refresh;
    HWND details_label;
    HWND details;
    HWND verify_label;
    HWND verify;
    HWND direct_io;
    HWND dismount;
    HWND write_button;
    HWND progress;
    HWND progress_text;
    HWND log_label;
    HWND log;
    HWND evidence_button;
    HFONT font;
    HFONT bold_font;
    HFONT mono;
    HBRUSH status_brush;
    COLORREF status_color;
    df_target_info targets[DF_GUI_MAX_TARGETS];
    unsigned target_count;
    unsigned blocked_system_count;
    bool elevated;
    bool scanning;
    bool source_hashing;
    bool planning;
    bool writing;
    bool source_ready;
    bool result_visible;
    wchar_t source_path_w[DF_MAX_PATH_CHARS];
    char source_path[DF_MAX_PATH_CHARS];
    uint64_t source_size;
    uint8_t source_sha256[32];
    wchar_t evidence_folder[DF_MAX_PATH_CHARS];
    df_progress_phase active_phase;
    ULONGLONG phase_started_ms;
    df_gui_scan_job scan_job;
    df_gui_source_job source_job;
    df_gui_operation_job operation_job;
} df_gui_state;

static df_gui_state g_gui;

static bool df_gui_busy(void) {
    return g_gui.scanning || g_gui.source_hashing ||
           g_gui.planning || g_gui.writing;
}

static void df_gui_startup_trace(const wchar_t *stage, DWORD code) {
    wchar_t temp[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t line[512];
    HANDLE file;
    DWORD written = 0u;
    int count;

    if (GetTempPathW(MAX_PATH, temp) == 0u) return;
    if (swprintf_s(path, MAX_PATH,
                   L"%lsdeadflash-gui-startup.log", temp) < 0) return;
    file = CreateFileW(path, FILE_APPEND_DATA,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;

    count = swprintf_s(line, sizeof(line) / sizeof(line[0]),
                       L"stage=%ls code=%lu\r\n",
                       stage, (unsigned long)code);
    if (count > 0) {
        (void)WriteFile(file, line,
                        (DWORD)((size_t)count * sizeof(wchar_t)),
                        &written, NULL);
    }
    CloseHandle(file);
}

static void df_gui_show_startup_error(const wchar_t *stage, DWORD code) {
    wchar_t system_message[512];
    wchar_t message[1024];
    DWORD length;

    system_message[0] = L'\0';
    length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, code, 0u, system_message,
                            (DWORD)(sizeof(system_message) /
                                    sizeof(system_message[0])),
                            NULL);
    if (length == 0u) {
        (void)wcscpy_s(system_message,
                       sizeof(system_message) /
                           sizeof(system_message[0]),
                       L"No Windows error text was available.");
    }

    (void)swprintf_s(message,
                     sizeof(message) / sizeof(message[0]),
                     L"DEADFLASH GUI failed during %ls.\r\n\r\n"
                     L"Windows error: %lu\r\n%ls\r\n\r\n"
                     L"Startup log: %%TEMP%%\\deadflash-gui-startup.log",
                     stage, (unsigned long)code, system_message);
    df_gui_startup_trace(stage, code);
    MessageBoxW(NULL, message, DF_GUI_TITLE,
                MB_OK | MB_ICONERROR | MB_TOPMOST);
}

static bool df_gui_utf8_to_wide(const char *input, wchar_t *output,
                                size_t output_count) {
    int result;
    if (input == NULL || output == NULL || output_count == 0u ||
        output_count > INT_MAX) return false;
    output[0] = L'\0';
    result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 input, -1, output,
                                 (int)output_count);
    return result > 0;
}

static bool df_gui_wide_to_utf8(const wchar_t *input, char *output,
                                size_t output_count) {
    int result;
    if (input == NULL || output == NULL || output_count == 0u ||
        output_count > INT_MAX) return false;
    output[0] = '\0';
    result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                 input, -1, output,
                                 (int)output_count, NULL, NULL);
    return result > 0;
}

static const wchar_t *df_gui_path_basename_w(const wchar_t *path) {
    const wchar_t *slash;
    const wchar_t *backslash;
    const wchar_t *base;
    if (path == NULL) return L"";
    slash = wcsrchr(path, L'/');
    backslash = wcsrchr(path, L'\\');
    base = slash != NULL ? slash + 1 : path;
    if (backslash != NULL && backslash + 1 > base) base = backslash + 1;
    return base;
}

static void df_gui_format_size(uint64_t bytes, wchar_t output[64]) {
    if (bytes >= UINT64_C(1099511627776)) {
        (void)swprintf_s(output, 64u, L"%.2f TiB",
                         (double)bytes / 1099511627776.0);
    } else if (bytes >= UINT64_C(1073741824)) {
        (void)swprintf_s(output, 64u, L"%.2f GiB",
                         (double)bytes / 1073741824.0);
    } else if (bytes >= UINT64_C(1048576)) {
        (void)swprintf_s(output, 64u, L"%.2f MiB",
                         (double)bytes / 1048576.0);
    } else if (bytes >= UINT64_C(1024)) {
        (void)swprintf_s(output, 64u, L"%.2f KiB",
                         (double)bytes / 1024.0);
    } else {
        (void)swprintf_s(output, 64u, L"%llu bytes",
                         (unsigned long long)bytes);
    }
}

static void df_gui_format_duration(uint64_t seconds, wchar_t output[32]) {
    const uint64_t hours = seconds / 3600u;
    const uint64_t minutes = (seconds % 3600u) / 60u;
    const uint64_t secs = seconds % 60u;
    if (hours != 0u) {
        (void)swprintf_s(output, 32u, L"%02llu:%02llu:%02llu",
                         (unsigned long long)hours,
                         (unsigned long long)minutes,
                         (unsigned long long)secs);
    } else {
        (void)swprintf_s(output, 32u, L"%02llu:%02llu",
                         (unsigned long long)minutes,
                         (unsigned long long)secs);
    }
}

static void df_gui_set_font(HWND control, HFONT font) {
    if (control != NULL && font != NULL) {
        (void)SendMessageW(control, WM_SETFONT,
                           (WPARAM)font, (LPARAM)TRUE);
    }
}

static void df_gui_append_log(const wchar_t *text) {
    LRESULT length;
    if (g_gui.log == NULL || text == NULL) return;
    length = SendMessageW(g_gui.log, WM_GETTEXTLENGTH, 0u, 0);
    (void)SendMessageW(g_gui.log, EM_SETSEL,
                       (WPARAM)length, (LPARAM)length);
    (void)SendMessageW(g_gui.log, EM_REPLACESEL,
                       (WPARAM)FALSE, (LPARAM)text);
    (void)SendMessageW(g_gui.log, EM_SCROLLCARET, 0u, 0);
}

static COLORREF df_gui_status_color(df_gui_status_kind kind) {
    switch (kind) {
        case DF_GUI_STATUS_BUSY: return RGB(25, 88, 138);
        case DF_GUI_STATUS_READY: return RGB(20, 112, 68);
        case DF_GUI_STATUS_WARNING: return RGB(150, 96, 0);
        case DF_GUI_STATUS_ERROR: return RGB(155, 38, 38);
        case DF_GUI_STATUS_COMPLETE: return RGB(15, 120, 80);
        case DF_GUI_STATUS_IDLE:
        default: return RGB(70, 74, 82);
    }
}

static void df_gui_set_status(const wchar_t *text,
                              df_gui_status_kind kind) {
    HBRUSH replacement;
    const COLORREF color = df_gui_status_color(kind);
    if (g_gui.status == NULL) return;
    replacement = CreateSolidBrush(color);
    if (replacement != NULL) {
        if (g_gui.status_brush != NULL) {
            (void)DeleteObject(g_gui.status_brush);
        }
        g_gui.status_brush = replacement;
        g_gui.status_color = color;
    }
    SetWindowTextW(g_gui.status, text);
    InvalidateRect(g_gui.status, NULL, TRUE);
}

static void df_gui_set_progress_mode(bool marquee, int state, int percent) {
    if (g_gui.progress == NULL) return;
    (void)SendMessageW(g_gui.progress, PBM_SETMARQUEE,
                       (WPARAM)(marquee ? TRUE : FALSE),
                       (LPARAM)(marquee ? 35 : 0));
    if (!marquee) {
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        (void)SendMessageW(g_gui.progress, PBM_SETPOS,
                           (WPARAM)percent, 0);
    }
    (void)SendMessageW(g_gui.progress, PBM_SETSTATE,
                       (WPARAM)state, 0);
}

static bool df_gui_is_elevated(void) {
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation;
    DWORD returned = 0u;
    bool elevated = false;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
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
        df_gui_show_startup_error(L"resolve executable path",
                                  GetLastError());
        return;
    }
    if ((INT_PTR)ShellExecuteW(g_gui.window, L"runas", executable,
                               NULL, NULL, SW_SHOWNORMAL) <= 32) {
        MessageBoxW(g_gui.window,
                    L"Administrator access was not granted.",
                    DF_GUI_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    DestroyWindow(g_gui.window);
}

static HWND df_gui_control(const wchar_t *class_name,
                           const wchar_t *text,
                           DWORD style, DWORD ex_style,
                           int id) {
    return CreateWindowExW(ex_style, class_name, text, style,
                           0, 0, 10, 10, g_gui.window,
                           (HMENU)(INT_PTR)id,
                           GetModuleHandleW(NULL), NULL);
}

static void df_gui_layout(void) {
    RECT rect;
    int width;
    int height;
    int margin = 18;
    int button_width = 145;
    int gap = 12;
    int content_width;
    int field_width;
    int log_top = 584;
    int log_bottom;
    int log_height;

    if (g_gui.window == NULL) return;
    if (!GetClientRect(g_gui.window, &rect)) return;
    width = (int)(rect.right - rect.left);
    height = (int)(rect.bottom - rect.top);
    content_width = width - margin * 2;
    field_width = content_width - button_width - gap;
    log_bottom = height - 58;
    log_height = log_bottom - log_top;
    if (log_height < 100) log_height = 100;

    (void)MoveWindow(g_gui.header, margin, 12,
                     content_width - 260, 24, TRUE);
    (void)MoveWindow(g_gui.admin, width - margin - 250, 12,
                     250, 24, TRUE);
    (void)MoveWindow(g_gui.elevate, width - margin - button_width,
                     40, button_width, 28, TRUE);
    (void)MoveWindow(g_gui.status, margin, 44,
                     content_width, 34, TRUE);

    (void)MoveWindow(g_gui.source_label, margin, 92,
                     180, 20, TRUE);
    (void)MoveWindow(g_gui.source, margin, 114,
                     field_width, 28, TRUE);
    (void)MoveWindow(g_gui.browse,
                     margin + field_width + gap, 113,
                     button_width, 30, TRUE);
    (void)MoveWindow(g_gui.source_info, margin, 148,
                     content_width, 46, TRUE);

    (void)MoveWindow(g_gui.target_label, margin, 208,
                     180, 20, TRUE);
    (void)MoveWindow(g_gui.target, margin, 230,
                     field_width, 260, TRUE);
    (void)MoveWindow(g_gui.refresh,
                     margin + field_width + gap, 229,
                     button_width, 30, TRUE);

    (void)MoveWindow(g_gui.details_label, margin, 272,
                     180, 20, TRUE);
    (void)MoveWindow(g_gui.details, margin, 294,
                     content_width, 126, TRUE);

    (void)MoveWindow(g_gui.verify_label, margin, 438,
                     90, 20, TRUE);
    (void)MoveWindow(g_gui.verify, margin, 460,
                     270, 180, TRUE);
    (void)MoveWindow(g_gui.direct_io, margin + 290, 459,
                     130, 28, TRUE);
    (void)MoveWindow(g_gui.dismount, margin + 435, 459,
                     250, 28, TRUE);
    (void)MoveWindow(g_gui.write_button,
                     width - margin - 190, 448,
                     190, 44, TRUE);

    (void)MoveWindow(g_gui.progress, margin, 508,
                     content_width, 20, TRUE);
    (void)MoveWindow(g_gui.progress_text, margin, 532,
                     content_width, 24, TRUE);

    (void)MoveWindow(g_gui.log_label, margin, 562,
                     180, 20, TRUE);
    (void)MoveWindow(g_gui.log, margin, log_top,
                     content_width, log_height, TRUE);
    (void)MoveWindow(g_gui.evidence_button,
                     width - margin - 190, height - 44,
                     190, 30, TRUE);
}

static bool df_gui_create_controls(void) {
    LOGFONTW logical_font;
    HFONT stock = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    if (stock == NULL ||
        GetObjectW(stock, (int)sizeof(logical_font),
                   &logical_font) == 0) return false;

    g_gui.font = CreateFontIndirectW(&logical_font);
    logical_font.lfWeight = FW_BOLD;
    g_gui.bold_font = CreateFontIndirectW(&logical_font);
    logical_font.lfWeight = FW_NORMAL;
    logical_font.lfHeight = -15;
    (void)wcscpy_s(logical_font.lfFaceName,
                   LF_FACESIZE, L"Consolas");
    g_gui.mono = CreateFontIndirectW(&logical_font);
    if (g_gui.font == NULL || g_gui.bold_font == NULL ||
        g_gui.mono == NULL) return false;

    g_gui.header = df_gui_control(L"STATIC",
        L"DEADFLASH 1.0.0   |   WRITE THE IMAGE. VERIFY THE TRUTH.",
        WS_CHILD | WS_VISIBLE, 0u, 0);
    g_gui.admin = df_gui_control(L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT, 0u, IDC_ADMIN);
    g_gui.elevate = df_gui_control(L"BUTTON", L"RUN AS ADMIN",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0u, IDC_ELEVATE);
    g_gui.status = df_gui_control(L"STATIC", L"STARTING",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        0u, IDC_STATUS);

    g_gui.source_label = df_gui_control(L"STATIC", L"SOURCE IMAGE",
        WS_CHILD | WS_VISIBLE, 0u, 0);
    g_gui.source = df_gui_control(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
        WS_EX_CLIENTEDGE, IDC_SOURCE);
    g_gui.browse = df_gui_control(L"BUTTON", L"SELECT IMAGE",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0u, IDC_BROWSE);
    g_gui.source_info = df_gui_control(L"EDIT",
        L"Select or drop an IMG, ISO, or BIN file. "
        L"DEADFLASH will preflight its size and SHA-256.",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE |
            ES_READONLY | ES_AUTOVSCROLL,
        WS_EX_CLIENTEDGE, IDC_SOURCE_INFO);

    g_gui.target_label = df_gui_control(L"STATIC", L"TARGET DEVICE",
        WS_CHILD | WS_VISIBLE, 0u, 0);
    g_gui.target = df_gui_control(WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            CBS_DROPDOWNLIST,
        WS_EX_CLIENTEDGE, IDC_TARGET);
    g_gui.refresh = df_gui_control(L"BUTTON", L"REFRESH DEVICES",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0u, IDC_REFRESH);

    g_gui.details_label = df_gui_control(L"STATIC",
        L"DEVICE & SAFETY OVERVIEW",
        WS_CHILD | WS_VISIBLE, 0u, 0);
    g_gui.details = df_gui_control(L"EDIT",
        L"Waiting for device scan.",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE |
            ES_READONLY | ES_AUTOVSCROLL,
        WS_EX_CLIENTEDGE, IDC_DETAILS);

    g_gui.verify_label = df_gui_control(L"STATIC", L"VERIFY",
        WS_CHILD | WS_VISIBLE, 0u, 0);
    g_gui.verify = df_gui_control(WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        WS_EX_CLIENTEDGE, IDC_VERIFY);
    g_gui.direct_io = df_gui_control(L"BUTTON", L"DIRECT I/O",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0u, IDC_DIRECT);
    g_gui.dismount = df_gui_control(L"BUTTON",
        L"LOCK + DISMOUNT VOLUMES",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0u, IDC_DISMOUNT);
    g_gui.write_button = df_gui_control(L"BUTTON",
        L"WRITE + VERIFY",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0u, IDC_WRITE);

    g_gui.progress = df_gui_control(PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH | PBS_MARQUEE,
        0u, IDC_PROGRESS);
    g_gui.progress_text = df_gui_control(L"STATIC",
        L"IDLE", WS_CHILD | WS_VISIBLE, 0u, IDC_PROGRESS_TEXT);

    g_gui.log_label = df_gui_control(L"STATIC", L"OPERATION LOG",
        WS_CHILD | WS_VISIBLE, 0u, 0);
    g_gui.log = df_gui_control(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        WS_EX_CLIENTEDGE, IDC_LOG);
    g_gui.evidence_button = df_gui_control(L"BUTTON",
        L"OPEN EVIDENCE FOLDER",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0u, IDC_EVIDENCE);

    if (g_gui.header == NULL || g_gui.admin == NULL ||
        g_gui.elevate == NULL || g_gui.status == NULL ||
        g_gui.source_label == NULL || g_gui.source == NULL ||
        g_gui.browse == NULL || g_gui.source_info == NULL ||
        g_gui.target_label == NULL || g_gui.target == NULL ||
        g_gui.refresh == NULL || g_gui.details_label == NULL ||
        g_gui.details == NULL || g_gui.verify_label == NULL ||
        g_gui.verify == NULL || g_gui.direct_io == NULL ||
        g_gui.dismount == NULL || g_gui.write_button == NULL ||
        g_gui.progress == NULL || g_gui.progress_text == NULL ||
        g_gui.log_label == NULL || g_gui.log == NULL ||
        g_gui.evidence_button == NULL) return false;

    df_gui_set_font(g_gui.header, g_gui.bold_font);
    df_gui_set_font(g_gui.admin, g_gui.font);
    df_gui_set_font(g_gui.elevate, g_gui.font);
    df_gui_set_font(g_gui.status, g_gui.bold_font);
    df_gui_set_font(g_gui.source_label, g_gui.bold_font);
    df_gui_set_font(g_gui.source, g_gui.font);
    df_gui_set_font(g_gui.browse, g_gui.font);
    df_gui_set_font(g_gui.source_info, g_gui.mono);
    df_gui_set_font(g_gui.target_label, g_gui.bold_font);
    df_gui_set_font(g_gui.target, g_gui.font);
    df_gui_set_font(g_gui.refresh, g_gui.font);
    df_gui_set_font(g_gui.details_label, g_gui.bold_font);
    df_gui_set_font(g_gui.details, g_gui.mono);
    df_gui_set_font(g_gui.verify_label, g_gui.bold_font);
    df_gui_set_font(g_gui.verify, g_gui.font);
    df_gui_set_font(g_gui.direct_io, g_gui.font);
    df_gui_set_font(g_gui.dismount, g_gui.font);
    df_gui_set_font(g_gui.write_button, g_gui.bold_font);
    df_gui_set_font(g_gui.progress_text, g_gui.mono);
    df_gui_set_font(g_gui.log_label, g_gui.bold_font);
    df_gui_set_font(g_gui.log, g_gui.mono);
    df_gui_set_font(g_gui.evidence_button, g_gui.font);

    (void)SendMessageW(g_gui.verify, CB_ADDSTRING, 0u,
                       (LPARAM)L"FULL READBACK — PROVEN");
    (void)SendMessageW(g_gui.verify, CB_ADDSTRING, 0u,
                       (LPARAM)L"SAMPLED READBACK — PARTIAL");
    (void)SendMessageW(g_gui.verify, CB_ADDSTRING, 0u,
                       (LPARAM)L"NO READBACK — UNPROVEN");
    (void)SendMessageW(g_gui.verify, CB_SETCURSEL, 0u, 0);
    (void)SendMessageW(g_gui.dismount, BM_SETCHECK,
                       BST_CHECKED, 0);
    (void)SendMessageW(g_gui.progress, PBM_SETRANGE,
                       0u, MAKELPARAM(0, 100));
    (void)SendMessageW(g_gui.log, EM_SETLIMITTEXT,
                       (WPARAM)(1024u * 1024u), 0);
    EnableWindow(g_gui.evidence_button, FALSE);

    g_gui.elevated = df_gui_is_elevated();
    SetWindowTextW(g_gui.admin,
                   g_gui.elevated ?
                       L"ADMINISTRATOR: YES" :
                       L"ADMINISTRATOR: NO");
    ShowWindow(g_gui.elevate,
               g_gui.elevated ? SW_HIDE : SW_SHOW);

    df_gui_layout();
    df_gui_set_status(L"INITIALIZING DEVICE SCAN",
                      DF_GUI_STATUS_BUSY);
    df_gui_set_progress_mode(true, PBST_NORMAL, 0);
    df_gui_append_log(L"[START] DEADFLASH 1.0.0 native Win32 frontend\r\n");
    df_gui_append_log(L"[SAFETY] system disks are hidden and blocked\r\n");
    df_gui_append_log(L"[SAFETY] writes require a fresh attested plan seal\r\n");
    return true;
}

static int df_gui_selected_target(void) {
    LRESULT selection = SendMessageW(g_gui.target,
                                     CB_GETCURSEL, 0u, 0);
    LRESULT data;
    if (selection == CB_ERR) return -1;
    data = SendMessageW(g_gui.target, CB_GETITEMDATA,
                        (WPARAM)selection, 0);
    if (data == CB_ERR || data < 0 ||
        (unsigned)data >= g_gui.target_count) return -1;
    return (int)data;
}

static bool df_gui_source_fits_target(const df_target_info *target) {
    uint64_t sector;
    uint64_t remainder;
    uint64_t rounded;
    if (target == NULL || !g_gui.source_ready) return false;
    sector = target->logical_sector_size != 0u ?
             target->logical_sector_size : 1u;
    remainder = g_gui.source_size % sector;
    rounded = g_gui.source_size;
    if (remainder != 0u) {
        const uint64_t addition = sector - remainder;
        if (rounded > UINT64_MAX - addition) return false;
        rounded += addition;
    }
    return rounded <= target->size_bytes;
}

static void df_gui_enable_operation_controls(bool enabled) {
    EnableWindow(g_gui.browse, enabled ? TRUE : FALSE);
    EnableWindow(g_gui.target, enabled ? TRUE : FALSE);
    EnableWindow(g_gui.refresh, enabled ? TRUE : FALSE);
    EnableWindow(g_gui.verify, enabled ? TRUE : FALSE);
    EnableWindow(g_gui.direct_io, enabled ? TRUE : FALSE);
    EnableWindow(g_gui.dismount, enabled ? TRUE : FALSE);
}

static void df_gui_update_ready(bool update_status) {
    const int selected = df_gui_selected_target();
    bool enabled = g_gui.elevated && !df_gui_busy() &&
                   g_gui.source_ready && selected >= 0;
    const df_target_info *target = NULL;
    const int verify = (int)SendMessageW(g_gui.verify,
                                         CB_GETCURSEL, 0u, 0);

    if (selected >= 0) target = &g_gui.targets[(unsigned)selected];
    if (target != NULL) {
        if (target->system_disk || target->read_only ||
            target->size_bytes == 0u ||
            !df_gui_source_fits_target(target)) enabled = false;
        if (target->mounted &&
            SendMessageW(g_gui.dismount, BM_GETCHECK,
                         0u, 0) != BST_CHECKED) enabled = false;
    }

    EnableWindow(g_gui.write_button, enabled ? TRUE : FALSE);
    SetWindowTextW(g_gui.write_button,
                   verify == 2 ?
                       L"WRITE UNPROVEN" :
                       L"WRITE + VERIFY");

    if (!update_status || df_gui_busy() || g_gui.result_visible) return;
    df_gui_set_progress_mode(false, PBST_NORMAL, 0);
    SetWindowTextW(g_gui.progress_text, L"IDLE");

    if (!g_gui.elevated) {
        df_gui_set_status(L"BLOCKED — ADMINISTRATOR ACCESS REQUIRED",
                          DF_GUI_STATUS_ERROR);
    } else if (!g_gui.source_ready) {
        df_gui_set_status(L"SELECT A SOURCE IMAGE",
                          DF_GUI_STATUS_IDLE);
    } else if (target == NULL) {
        df_gui_set_status(L"CONNECT AND SELECT A TARGET DEVICE",
                          DF_GUI_STATUS_IDLE);
    } else if (target->read_only) {
        df_gui_set_status(L"BLOCKED — TARGET IS READ-ONLY",
                          DF_GUI_STATUS_ERROR);
    } else if (!df_gui_source_fits_target(target)) {
        df_gui_set_status(L"BLOCKED — SOURCE IMAGE EXCEEDS TARGET",
                          DF_GUI_STATUS_ERROR);
    } else if (target->mounted &&
               SendMessageW(g_gui.dismount, BM_GETCHECK,
                            0u, 0) != BST_CHECKED) {
        df_gui_set_status(L"BLOCKED — TARGET HAS MOUNTED VOLUMES",
                          DF_GUI_STATUS_ERROR);
    } else if (verify == 2) {
        df_gui_set_status(L"READY — READBACK VERIFICATION DISABLED",
                          DF_GUI_STATUS_WARNING);
    } else {
        df_gui_set_status(L"READY — SEALED WRITE + READBACK VERIFY",
                          DF_GUI_STATUS_READY);
    }
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
    const wchar_t *identity;
    const wchar_t *media;
    const wchar_t *safety;

    g_gui.result_visible = false;
    if (selected < 0) {
        SetWindowTextW(g_gui.details,
                       L"No target selected.");
        df_gui_update_ready(true);
        return;
    }

    {
        const df_target_info *target =
            &g_gui.targets[(unsigned)selected];
        (void)df_gui_utf8_to_wide(target->path, path,
                                  sizeof(path) / sizeof(path[0]));
        (void)df_gui_utf8_to_wide(
            target->vendor[0] != '\0' ? target->vendor : "-",
            vendor, sizeof(vendor) / sizeof(vendor[0]));
        (void)df_gui_utf8_to_wide(
            target->product[0] != '\0' ?
                target->product : "UNKNOWN DEVICE",
            product, sizeof(product) / sizeof(product[0]));
        (void)df_gui_utf8_to_wide(
            target->bus_type[0] != '\0' ?
                target->bus_type : "UNKNOWN",
            bus, sizeof(bus) / sizeof(bus[0]));
        (void)df_gui_utf8_to_wide(target->token, token,
                                  sizeof(token) / sizeof(token[0]));
        df_gui_format_size(target->size_bytes, size);
        identity = target->serial_bound ? L"SERIAL-BOUND" :
                   (target->descriptor_present ?
                        L"DESCRIPTOR-BOUND" : L"GEOMETRY-ONLY");
        media = target->removable ? L"REMOVABLE" : L"FIXED";
        safety = target->read_only ? L"BLOCKED — READ-ONLY" :
                 (!df_gui_source_fits_target(target) &&
                  g_gui.source_ready) ?
                    L"BLOCKED — SOURCE TOO LARGE" :
                 target->mounted ?
                    L"READY WITH LOCK + DISMOUNT" :
                    L"READY";

        (void)swprintf_s(
            text, sizeof(text) / sizeof(text[0]),
            L"DEVICE     %ls %ls\r\n"
            L"PATH       %ls\r\n"
            L"CAPACITY   %ls\r\n"
            L"LINK       %ls / %ls\r\n"
            L"SECTORS    %u logical / %u physical\r\n"
            L"IDENTITY   %ls\r\n"
            L"TOKEN      %ls\r\n"
            L"SAFETY     %ls",
            vendor, product, path, size, bus, media,
            target->logical_sector_size,
            target->physical_sector_size,
            identity, token, safety);
    }

    SetWindowTextW(g_gui.details, text);
    df_gui_update_ready(true);
}

static void df_gui_progress_callback(void *context,
                                     df_progress_phase phase,
                                     uint64_t completed,
                                     uint64_t total) {
    df_gui_progress_sink *sink =
        (df_gui_progress_sink *)context;
    df_gui_progress_event *event;

    if (sink == NULL || sink->owner == NULL) return;
    event = (df_gui_progress_event *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*event));
    if (event == NULL) return;
    event->phase = phase;
    event->completed = completed;
    event->total = total;
    if (!PostMessageW(sink->owner, DF_GUI_WM_PROGRESS,
                      0u, (LPARAM)event)) {
        (void)HeapFree(GetProcessHeap(), 0u, event);
    }
}

static const wchar_t *df_gui_phase_label(df_progress_phase phase) {
    switch (phase) {
        case DF_PROGRESS_PREPARE: return L"PREPARING TARGET";
        case DF_PROGRESS_HASH_SOURCE: return L"HASHING SOURCE";
        case DF_PROGRESS_WRITE: return L"WRITING IMAGE";
        case DF_PROGRESS_FLUSH: return L"FLUSHING DEVICE CACHE";
        case DF_PROGRESS_VERIFY: return L"VERIFYING READBACK";
        case DF_PROGRESS_COMPLETE: return L"FINALIZING EVIDENCE";
        default: return L"WORKING";
    }
}

static void df_gui_handle_progress(df_gui_progress_event *event) {
    wchar_t completed_text[64];
    wchar_t total_text[64];
    wchar_t duration[32];
    wchar_t line[256];
    int percent = 0;
    ULONGLONG now;
    uint64_t elapsed_seconds;
    double speed = 0.0;
    const wchar_t *label;

    if (event == NULL) return;
    if (event->total != 0u) {
        if (event->completed >= event->total) {
            percent = 100;
        } else {
            percent = (int)(((double)event->completed * 100.0) /
                            (double)event->total);
        }
    }

    if (event->phase != g_gui.active_phase) {
        wchar_t phase_line[128];
        g_gui.active_phase = event->phase;
        g_gui.phase_started_ms = GetTickCount64();
        label = df_gui_phase_label(event->phase);
        (void)swprintf_s(phase_line,
                         sizeof(phase_line) /
                             sizeof(phase_line[0]),
                         L"[PHASE] %ls\r\n", label);
        df_gui_append_log(phase_line);
    }

    now = GetTickCount64();
    elapsed_seconds =
        (uint64_t)((now - g_gui.phase_started_ms) / 1000u);
    if (elapsed_seconds != 0u) {
        speed = ((double)event->completed / 1048576.0) /
                (double)elapsed_seconds;
    }

    df_gui_format_size(event->completed, completed_text);
    df_gui_format_size(event->total, total_text);
    df_gui_format_duration(elapsed_seconds, duration);
    label = df_gui_phase_label(event->phase);

    if (g_gui.planning && event->phase == DF_PROGRESS_HASH_SOURCE) {
        label = L"SEALING OPERATION PLAN";
    } else if (g_gui.source_hashing &&
               event->phase == DF_PROGRESS_HASH_SOURCE) {
        label = L"PREFLIGHTING SOURCE IMAGE";
    } else if (g_gui.writing &&
               event->phase == DF_PROGRESS_HASH_SOURCE) {
        label = L"REVALIDATING SEALED SOURCE";
    }

    if (event->total != 0u &&
        (event->phase == DF_PROGRESS_HASH_SOURCE ||
         event->phase == DF_PROGRESS_WRITE ||
         event->phase == DF_PROGRESS_VERIFY)) {
        (void)swprintf_s(line,
                         sizeof(line) / sizeof(line[0]),
                         L"%ls   %d%%   %ls / %ls   %.1f MiB/s   %ls",
                         label, percent,
                         completed_text, total_text,
                         speed, duration);
    } else {
        (void)swprintf_s(line,
                         sizeof(line) / sizeof(line[0]),
                         L"%ls   %d%%   %ls",
                         label, percent, duration);
    }

    SetWindowTextW(g_gui.progress_text, line);
    df_gui_set_status(label, DF_GUI_STATUS_BUSY);
    df_gui_set_progress_mode(false, PBST_NORMAL, percent);
}

static DWORD WINAPI df_gui_scan_thread(LPVOID parameter) {
    df_gui_scan_job *job = (df_gui_scan_job *)parameter;
    unsigned disk;

    job->count = 0u;
    job->blocked_system_count = 0u;
    for (disk = 0u; disk < DF_GUI_MAX_TARGETS; ++disk) {
        char path[64];
        df_target_info info;
        df_error error;

        (void)snprintf(path, sizeof(path),
                       "\\\\.\\PhysicalDrive%u", disk);
        if (df_inspect_target(path, &info, &error) != DF_OK) continue;
        if (info.kind != DF_TARGET_BLOCK_DEVICE ||
            info.size_bytes == 0u) continue;
        if (info.system_disk) {
            job->blocked_system_count++;
            continue;
        }
        if (job->count < DF_GUI_MAX_TARGETS) {
            job->targets[job->count++] = info;
        }
    }

    (void)PostMessageW(job->owner, DF_GUI_WM_SCAN_DONE, 0u, 0);
    return 0u;
}

static void df_gui_begin_scan(void) {
    HANDLE thread;
    if (df_gui_busy()) return;

    g_gui.result_visible = false;
    g_gui.scanning = true;
    g_gui.scan_job.owner = g_gui.window;
    g_gui.scan_job.count = 0u;
    g_gui.scan_job.blocked_system_count = 0u;
    df_gui_enable_operation_controls(false);
    EnableWindow(g_gui.write_button, FALSE);
    SetWindowTextW(g_gui.details,
                   L"Scanning PhysicalDrive0..31 on a worker thread.\r\n"
                   L"Windows system disks are hidden and never offered.");
    SetWindowTextW(g_gui.progress_text, L"SCANNING PHYSICAL DRIVES");
    df_gui_set_status(L"SCANNING PHYSICAL DRIVES",
                      DF_GUI_STATUS_BUSY);
    df_gui_set_progress_mode(true, PBST_NORMAL, 0);
    df_gui_append_log(L"[SCAN] physical drive scan started\r\n");

    thread = CreateThread(NULL, 0u, df_gui_scan_thread,
                          &g_gui.scan_job, 0u, NULL);
    if (thread == NULL) {
        g_gui.scanning = false;
        df_gui_enable_operation_controls(true);
        MessageBoxW(g_gui.window,
                    L"Could not start the device scan worker.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        df_gui_update_ready(true);
        return;
    }
    CloseHandle(thread);
}

static void df_gui_finish_scan(void) {
    unsigned i;
    int first_usable = -1;
    wchar_t line[256];

    g_gui.scanning = false;
    (void)SendMessageW(g_gui.target, CB_RESETCONTENT, 0u, 0);
    g_gui.target_count = g_gui.scan_job.count;
    g_gui.blocked_system_count =
        g_gui.scan_job.blocked_system_count;

    for (i = 0u; i < g_gui.target_count; ++i) {
        wchar_t path[DF_MAX_PATH_CHARS];
        wchar_t product[256];
        wchar_t size[64];
        wchar_t bus[64];
        wchar_t label[640];
        LRESULT item;

        g_gui.targets[i] = g_gui.scan_job.targets[i];
        (void)df_gui_utf8_to_wide(g_gui.targets[i].path,
                                  path,
                                  sizeof(path) / sizeof(path[0]));
        (void)df_gui_utf8_to_wide(
            g_gui.targets[i].product[0] != '\0' ?
                g_gui.targets[i].product : "UNKNOWN DEVICE",
            product, sizeof(product) / sizeof(product[0]));
        (void)df_gui_utf8_to_wide(
            g_gui.targets[i].bus_type[0] != '\0' ?
                g_gui.targets[i].bus_type : "UNKNOWN",
            bus, sizeof(bus) / sizeof(bus[0]));
        df_gui_format_size(g_gui.targets[i].size_bytes, size);

        (void)swprintf_s(
            label, sizeof(label) / sizeof(label[0]),
            L"%ls  |  %ls  |  %ls  |  %ls%ls",
            path, product, size, bus,
            g_gui.targets[i].read_only ?
                L"  |  READ-ONLY" : L"");
        item = SendMessageW(g_gui.target, CB_ADDSTRING,
                            0u, (LPARAM)label);
        if (item != CB_ERR && item != CB_ERRSPACE) {
            (void)SendMessageW(g_gui.target, CB_SETITEMDATA,
                               (WPARAM)item, (LPARAM)i);
            if (first_usable < 0 &&
                !g_gui.targets[i].read_only) {
                first_usable = (int)item;
            }
        }
    }

    df_gui_enable_operation_controls(true);
    if (g_gui.target_count == 0u) {
        SetWindowTextW(
            g_gui.details,
            L"No non-system physical target was found.\r\n\r\n"
            L"Connect a USB drive, then press REFRESH DEVICES.");
        df_gui_append_log(L"[SCAN] no usable non-system target found\r\n");
    } else {
        const int selection =
            first_usable >= 0 ? first_usable : 0;
        (void)SendMessageW(g_gui.target, CB_SETCURSEL,
                           (WPARAM)selection, 0);
        df_gui_show_target();
    }

    (void)swprintf_s(
        line, sizeof(line) / sizeof(line[0]),
        L"[SCAN] %u target(s) visible; %u system disk(s) hidden\r\n",
        g_gui.target_count, g_gui.blocked_system_count);
    df_gui_append_log(line);
    df_gui_update_ready(true);
}

static DWORD WINAPI df_gui_source_thread(LPVOID parameter) {
    df_gui_source_job *job = (df_gui_source_job *)parameter;
    df_error_clear(&job->error);
    job->status = df_hash_source_path(
        job->path, DF_DEFAULT_BUFFER_SIZE,
        &job->size_bytes, job->sha256,
        df_gui_progress_callback, &job->progress,
        &job->error);
    (void)PostMessageW(job->owner, DF_GUI_WM_SOURCE_DONE, 0u, 0);
    return 0u;
}

static void df_gui_begin_source(const wchar_t *path) {
    HANDLE thread;
    if (path == NULL || path[0] == L'\0' || df_gui_busy()) return;

    memset(&g_gui.source_job, 0, sizeof(g_gui.source_job));
    if (!df_gui_wide_to_utf8(path, g_gui.source_job.path,
                             sizeof(g_gui.source_job.path))) {
        MessageBoxW(g_gui.window,
                    L"The selected path could not be encoded as UTF-8.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    g_gui.result_visible = false;
    g_gui.source_ready = false;
    g_gui.source_hashing = true;
    g_gui.source_job.owner = g_gui.window;
    g_gui.source_job.progress.owner = g_gui.window;
    (void)wcscpy_s(g_gui.source_path_w,
                   sizeof(g_gui.source_path_w) /
                       sizeof(g_gui.source_path_w[0]),
                   path);
    (void)strcpy_s(g_gui.source_path,
                   sizeof(g_gui.source_path),
                   g_gui.source_job.path);
    SetWindowTextW(g_gui.source, path);
    SetWindowTextW(g_gui.source_info,
                   L"Reading source size and computing SHA-256...");
    df_gui_enable_operation_controls(false);
    EnableWindow(g_gui.write_button, FALSE);
    df_gui_set_status(L"PREFLIGHTING SOURCE IMAGE",
                      DF_GUI_STATUS_BUSY);
    df_gui_set_progress_mode(false, PBST_NORMAL, 0);
    SetWindowTextW(g_gui.progress_text,
                   L"PREFLIGHTING SOURCE IMAGE");
    df_gui_append_log(L"[SOURCE] preflight hash started\r\n");

    thread = CreateThread(NULL, 0u, df_gui_source_thread,
                          &g_gui.source_job, 0u, NULL);
    if (thread == NULL) {
        g_gui.source_hashing = false;
        df_gui_enable_operation_controls(true);
        MessageBoxW(g_gui.window,
                    L"Could not start the source preflight worker.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        df_gui_update_ready(true);
        return;
    }
    CloseHandle(thread);
}

static void df_gui_finish_source(void) {
    wchar_t size[64];
    wchar_t hash[DF_SHA256_HEX_CHARS + 1u];
    wchar_t text[1024];
    char hash_hex[DF_SHA256_HEX_CHARS + 1u];

    g_gui.source_hashing = false;
    df_gui_enable_operation_controls(true);
    if (g_gui.source_job.status != DF_OK ||
        g_gui.source_job.size_bytes == 0u) {
        wchar_t error_text[DF_MAX_ERROR_MESSAGE];
        g_gui.source_ready = false;
        (void)df_gui_utf8_to_wide(
            g_gui.source_job.error.message[0] != '\0' ?
                g_gui.source_job.error.message :
                "The source image is empty or unreadable.",
            error_text,
            sizeof(error_text) / sizeof(error_text[0]));
        SetWindowTextW(g_gui.source_info, error_text);
        df_gui_set_status(L"SOURCE PREFLIGHT FAILED",
                          DF_GUI_STATUS_ERROR);
        df_gui_set_progress_mode(false, PBST_ERROR, 0);
        df_gui_append_log(L"[SOURCE] preflight failed\r\n");
        MessageBoxW(g_gui.window, error_text,
                    L"SOURCE PREFLIGHT FAILED",
                    MB_OK | MB_ICONERROR);
        df_gui_update_ready(false);
        return;
    }

    g_gui.source_ready = true;
    g_gui.source_size = g_gui.source_job.size_bytes;
    memcpy(g_gui.source_sha256, g_gui.source_job.sha256,
           sizeof(g_gui.source_sha256));
    df_hex_encode(g_gui.source_sha256, 32u, hash_hex);
    (void)df_gui_utf8_to_wide(hash_hex, hash,
                              sizeof(hash) / sizeof(hash[0]));
    df_gui_format_size(g_gui.source_size, size);
    (void)swprintf_s(
        text, sizeof(text) / sizeof(text[0]),
        L"FILE      %ls\r\n"
        L"SIZE      %ls\r\n"
        L"SHA-256   %ls\r\n"
        L"NOTE      final write uses a fresh attested hash",
        df_gui_path_basename_w(g_gui.source_path_w),
        size, hash);
    SetWindowTextW(g_gui.source_info, text);
    df_gui_set_progress_mode(false, PBST_NORMAL, 100);
    SetWindowTextW(g_gui.progress_text,
                   L"SOURCE PREFLIGHT COMPLETE");
    df_gui_append_log(L"[SOURCE] size and SHA-256 ready\r\n");
    if (df_gui_selected_target() >= 0) {
        df_gui_show_target();
    } else {
        df_gui_update_ready(true);
    }
}

static void df_gui_browse(void) {
    OPENFILENAMEW dialog;
    wchar_t path[DF_MAX_PATH_CHARS];

    if (df_gui_busy()) return;
    ZeroMemory(&dialog, sizeof(dialog));
    ZeroMemory(path, sizeof(path));
    dialog.lStructSize = (DWORD)sizeof(dialog);
    dialog.hwndOwner = g_gui.window;
    dialog.lpstrFilter =
        L"Disk images (*.img;*.iso;*.bin)\0"
        L"*.img;*.iso;*.bin\0"
        L"All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile =
        (DWORD)(sizeof(path) / sizeof(path[0]));
    dialog.Flags = OFN_FILEMUSTEXIST |
                   OFN_PATHMUSTEXIST |
                   OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Select source image";

    if (GetOpenFileNameW(&dialog)) {
        df_gui_begin_source(path);
    }
}

static bool df_gui_prepare_evidence_path(
    char output[DF_MAX_PATH_CHARS]) {
    wchar_t documents[MAX_PATH];
    wchar_t root[DF_MAX_PATH_CHARS];
    wchar_t evidence[DF_MAX_PATH_CHARS];
    wchar_t file_path[DF_MAX_PATH_CHARS];
    SYSTEMTIME now;
    DWORD error;

    if (SHGetFolderPathW(NULL,
                         CSIDL_PERSONAL | CSIDL_FLAG_CREATE,
                         NULL, SHGFP_TYPE_CURRENT,
                         documents) != S_OK) return false;
    if (swprintf_s(root,
                   sizeof(root) / sizeof(root[0]),
                   L"%ls\\DEADFLASH", documents) < 0) return false;
    if (!CreateDirectoryW(root, NULL)) {
        error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) return false;
    }
    if (swprintf_s(evidence,
                   sizeof(evidence) / sizeof(evidence[0]),
                   L"%ls\\Evidence", root) < 0) return false;
    if (!CreateDirectoryW(evidence, NULL)) {
        error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) return false;
    }

    GetLocalTime(&now);
    if (swprintf_s(
            file_path,
            sizeof(file_path) / sizeof(file_path[0]),
            L"%ls\\deadflash-evidence-%04u%02u%02u-%02u%02u%02u.json",
            evidence,
            (unsigned)now.wYear, (unsigned)now.wMonth,
            (unsigned)now.wDay, (unsigned)now.wHour,
            (unsigned)now.wMinute, (unsigned)now.wSecond) < 0) {
        return false;
    }

    (void)wcscpy_s(g_gui.evidence_folder,
                   sizeof(g_gui.evidence_folder) /
                       sizeof(g_gui.evidence_folder[0]),
                   evidence);
    return df_gui_wide_to_utf8(file_path, output,
                               DF_MAX_PATH_CHARS);
}

static const wchar_t *df_gui_verify_name(df_verify_mode mode) {
    switch (mode) {
        case DF_VERIFY_FULL: return L"FULL READBACK — PROVEN";
        case DF_VERIFY_SAMPLE: return L"SAMPLED READBACK — PARTIAL";
        case DF_VERIFY_NONE:
        default: return L"NO READBACK — UNPROVEN";
    }
}

static void df_gui_fill_options(df_gui_operation_job *job) {
    const int verify = (int)SendMessageW(g_gui.verify,
                                         CB_GETCURSEL, 0u, 0);
    memset(&job->options, 0, sizeof(job->options));
    job->options.buffer_size = DF_DEFAULT_BUFFER_SIZE;
    job->options.write_retries = 4u;
    job->options.sample_count = 64u;
    job->options.allow_device = true;
    job->options.force_mounted =
        SendMessageW(g_gui.dismount, BM_GETCHECK,
                     0u, 0) == BST_CHECKED;
    job->options.force_system_disk = false;
    job->options.direct_io =
        SendMessageW(g_gui.direct_io, BM_GETCHECK,
                     0u, 0) == BST_CHECKED;
    job->options.truncate_regular_file = true;
    job->options.verify_mode =
        verify == 1 ? DF_VERIFY_SAMPLE :
        (verify == 2 ? DF_VERIFY_NONE : DF_VERIFY_FULL);
    job->options.progress_callback = df_gui_progress_callback;
    job->options.progress_context = &job->progress;
}

static DWORD WINAPI df_gui_plan_thread(LPVOID parameter) {
    df_gui_operation_job *job =
        (df_gui_operation_job *)parameter;
    df_error_clear(&job->error);
    job->status = df_attest_plan(
        job->source_path, job->target.path,
        &job->options, &job->attestation,
        &job->error);
    (void)PostMessageW(job->owner, DF_GUI_WM_PLAN_DONE, 0u, 0);
    return 0u;
}

static void df_gui_begin_plan(void) {
    const int selected = df_gui_selected_target();
    df_gui_operation_job *job = &g_gui.operation_job;
    HANDLE thread;

    if (selected < 0 || df_gui_busy() ||
        !g_gui.source_ready || !g_gui.elevated) return;

    memset(job, 0, sizeof(*job));
    job->owner = g_gui.window;
    job->target = g_gui.targets[(unsigned)selected];
    job->progress.owner = g_gui.window;
    (void)strcpy_s(job->source_path,
                   sizeof(job->source_path),
                   g_gui.source_path);
    (void)strcpy_s(job->confirmation_token,
                   sizeof(job->confirmation_token),
                   job->target.token);
    df_gui_fill_options(job);
    job->options.confirmation_token =
        job->confirmation_token;

    if (!df_gui_prepare_evidence_path(job->report_path)) {
        MessageBoxW(
            g_gui.window,
            L"Could not create Documents\\DEADFLASH\\Evidence.",
            DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    g_gui.result_visible = false;
    g_gui.planning = true;
    g_gui.active_phase = DF_PROGRESS_COMPLETE;
    df_gui_enable_operation_controls(false);
    EnableWindow(g_gui.write_button, FALSE);
    df_gui_set_status(L"SEALING OPERATION PLAN",
                      DF_GUI_STATUS_BUSY);
    df_gui_set_progress_mode(false, PBST_NORMAL, 0);
    SetWindowTextW(g_gui.progress_text,
                   L"SEALING SOURCE + TARGET + POLICY");
    df_gui_append_log(L"[PLAN] building fresh operation seal\r\n");

    thread = CreateThread(NULL, 0u, df_gui_plan_thread,
                          job, 0u, NULL);
    if (thread == NULL) {
        g_gui.planning = false;
        df_gui_enable_operation_controls(true);
        MessageBoxW(g_gui.window,
                    L"Could not start the plan sealing worker.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        df_gui_update_ready(true);
        return;
    }
    CloseHandle(thread);
}

static bool df_gui_confirm_attested_plan(void) {
    df_gui_operation_job *job = &g_gui.operation_job;
    wchar_t source_hash[65];
    wchar_t source_size[64];
    wchar_t target_size[64];
    wchar_t product[256];
    wchar_t target_path[DF_MAX_PATH_CHARS];
    wchar_t token[64];
    wchar_t plan[65];
    wchar_t message[4096];
    char source_hex[65];

    df_hex_encode(job->attestation.source_sha256,
                  32u, source_hex);
    (void)df_gui_utf8_to_wide(source_hex, source_hash,
                              sizeof(source_hash) /
                                  sizeof(source_hash[0]));
    (void)df_gui_utf8_to_wide(
        job->attestation.target.product[0] != '\0' ?
            job->attestation.target.product :
            "UNKNOWN DEVICE",
        product, sizeof(product) / sizeof(product[0]));
    (void)df_gui_utf8_to_wide(
        job->attestation.target.path,
        target_path,
        sizeof(target_path) / sizeof(target_path[0]));
    (void)df_gui_utf8_to_wide(
        job->attestation.target.token,
        token, sizeof(token) / sizeof(token[0]));
    (void)df_gui_utf8_to_wide(
        job->attestation.plan_hex,
        plan, sizeof(plan) / sizeof(plan[0]));
    df_gui_format_size(job->attestation.source_size,
                       source_size);
    df_gui_format_size(job->attestation.target.size_bytes,
                       target_size);

    (void)swprintf_s(
        message, sizeof(message) / sizeof(message[0]),
        L"DESTROY ALL DATA ON THIS TARGET?\r\n\r\n"
        L"SOURCE\r\n"
        L"  %ls\r\n"
        L"  Size: %ls\r\n"
        L"  SHA-256: %ls\r\n\r\n"
        L"TARGET\r\n"
        L"  %ls\r\n"
        L"  %ls\r\n"
        L"  Capacity: %ls\r\n"
        L"  Token: %ls\r\n\r\n"
        L"POLICY\r\n"
        L"  Verify: %ls\r\n"
        L"  Direct I/O: %ls\r\n"
        L"  Lock + dismount: %ls\r\n\r\n"
        L"PLAN SEAL\r\n"
        L"  %ls\r\n\r\n"
        L"The sealed plan will be revalidated immediately before "
        L"the first destructive write.\r\n"
        L"This action cannot be undone.",
        df_gui_path_basename_w(g_gui.source_path_w),
        source_size, source_hash,
        product, target_path, target_size, token,
        df_gui_verify_name(job->options.verify_mode),
        job->options.direct_io ? L"YES" : L"NO",
        job->options.force_mounted ? L"YES" : L"NO",
        plan);

    return MessageBoxW(
        g_gui.window, message,
        L"FINAL DESTRUCTIVE CONFIRMATION",
        MB_YESNO | MB_ICONWARNING |
            MB_DEFBUTTON2 | MB_TOPMOST) == IDYES;
}

static DWORD WINAPI df_gui_write_thread(LPVOID parameter) {
    df_gui_operation_job *job =
        (df_gui_operation_job *)parameter;
    df_report_context context;

    memset(&job->result, 0, sizeof(job->result));
    df_error_clear(&job->error);
    df_error_clear(&job->report_error);
    job->status = df_write_image_attested(
        job->source_path, job->target.path,
        &job->options, job->attestation.plan_hex,
        &job->target, &job->result, &job->error);

    memset(&context, 0, sizeof(context));
    context.operation = "write_attested_gui";
    context.source_path = job->source_path;
    context.target_path = job->target.path;
    context.target = &job->target;
    context.write_options = &job->options;
    context.write_result = &job->result;
    context.status = job->status;
    context.error = &job->error;
    job->report_written =
        df_write_json_report(job->report_path,
                             &context,
                             &job->report_error) == DF_OK;

    (void)PostMessageW(job->owner,
                       DF_GUI_WM_WRITE_DONE, 0u, 0);
    return 0u;
}

static void df_gui_finish_plan(void) {
    df_gui_operation_job *job = &g_gui.operation_job;
    HANDLE thread;

    g_gui.planning = false;
    if (job->status != DF_OK) {
        wchar_t error_text[DF_MAX_ERROR_MESSAGE];
        (void)df_gui_utf8_to_wide(
            job->error.message[0] != '\0' ?
                job->error.message :
                "Could not create the operation plan seal.",
            error_text,
            sizeof(error_text) / sizeof(error_text[0]));
        df_gui_enable_operation_controls(true);
        df_gui_set_status(L"PLAN SEAL FAILED",
                          DF_GUI_STATUS_ERROR);
        df_gui_set_progress_mode(false, PBST_ERROR, 0);
        df_gui_append_log(L"[PLAN] failed before confirmation\r\n");
        MessageBoxW(g_gui.window, error_text,
                    L"PLAN SEAL FAILED",
                    MB_OK | MB_ICONERROR);
        df_gui_update_ready(false);
        return;
    }

    df_gui_append_log(L"[PLAN] source, target, and policy sealed\r\n");
    if (!df_gui_confirm_attested_plan()) {
        df_gui_enable_operation_controls(true);
        df_gui_append_log(L"[PLAN] destructive operation cancelled\r\n");
        df_gui_set_status(L"WRITE CANCELLED — NO MEDIA CHANGED",
                          DF_GUI_STATUS_IDLE);
        df_gui_set_progress_mode(false, PBST_NORMAL, 0);
        SetWindowTextW(g_gui.progress_text,
                       L"CANCELLED BEFORE WRITE");
        df_gui_update_ready(false);
        return;
    }

    g_gui.writing = true;
    g_gui.active_phase = DF_PROGRESS_COMPLETE;
    df_gui_set_status(L"REVALIDATING SEALED PLAN",
                      DF_GUI_STATUS_BUSY);
    df_gui_set_progress_mode(false, PBST_NORMAL, 0);
    SetWindowTextW(g_gui.progress_text,
                   L"NO MEDIA WRITE HAS STARTED YET");
    df_gui_append_log(
        L"[WRITE] confirmation accepted; revalidating plan\r\n");

    thread = CreateThread(NULL, 0u, df_gui_write_thread,
                          job, 0u, NULL);
    if (thread == NULL) {
        g_gui.writing = false;
        df_gui_enable_operation_controls(true);
        MessageBoxW(g_gui.window,
                    L"Could not start the write worker.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        df_gui_update_ready(true);
        return;
    }
    CloseHandle(thread);
}

static void df_gui_finish_write(void) {
    df_gui_operation_job *job = &g_gui.operation_job;
    wchar_t state[96];
    wchar_t status[96];
    wchar_t error_text[DF_MAX_ERROR_MESSAGE];
    wchar_t report[DF_MAX_PATH_CHARS];
    wchar_t line[2048];
    const bool verified =
        job->status == DF_OK &&
        job->options.verify_mode != DF_VERIFY_NONE;

    g_gui.writing = false;
    g_gui.result_visible = true;
    df_gui_enable_operation_controls(true);

    (void)df_gui_utf8_to_wide(job->result.final_state,
                              state,
                              sizeof(state) /
                                  sizeof(state[0]));
    (void)df_gui_utf8_to_wide(df_status_name(job->status),
                              status,
                              sizeof(status) /
                                  sizeof(status[0]));
    (void)df_gui_utf8_to_wide(
        job->error.message[0] != '\0' ?
            job->error.message : "-",
        error_text,
        sizeof(error_text) / sizeof(error_text[0]));
    (void)df_gui_utf8_to_wide(job->report_path,
                              report,
                              sizeof(report) /
                                  sizeof(report[0]));

    (void)swprintf_s(
        line, sizeof(line) / sizeof(line[0]),
        L"[RESULT] state=%ls status=%ls\r\n"
        L"[BYTES] written=%llu verified=%llu retries=%llu\r\n"
        L"[TIMING] hash=%.3f write=%.3f flush=%.3f "
        L"verify=%.3f total=%.3f ms\r\n"
        L"[ERROR] %ls\r\n"
        L"[EVIDENCE] %ls%ls\r\n",
        state, status,
        (unsigned long long)job->result.bytes_written,
        (unsigned long long)job->result.bytes_verified,
        (unsigned long long)job->result.write_retries,
        job->result.source_hash_ms,
        job->result.write_ms,
        job->result.flush_ms,
        job->result.verify_ms,
        job->result.total_ms,
        error_text,
        job->report_written ? L"" : L"FAILED: ",
        report);
    df_gui_append_log(line);

    EnableWindow(g_gui.evidence_button,
                 job->report_written ? TRUE : FALSE);
    df_gui_update_ready(false);

    if (job->status == DF_OK) {
        df_gui_set_progress_mode(
            false,
            verified && job->report_written ?
                PBST_NORMAL : PBST_PAUSED,
            100);
        if (!job->report_written) {
            df_gui_set_status(
                verified ?
                    L"COMPLETE — VERIFIED, BUT EVIDENCE FILE FAILED" :
                    L"COMPLETE — UNPROVEN, AND EVIDENCE FILE FAILED",
                DF_GUI_STATUS_WARNING);
            SetWindowTextW(
                g_gui.progress_text,
                L"MEDIA OPERATION SUCCEEDED — JSON EVIDENCE WAS NOT SAVED");
            MessageBoxW(
                g_gui.window,
                L"The media operation succeeded, but DEADFLASH "
                L"could not save the JSON evidence file.\r\n\r\n"
                L"Read the operation log and do not count this "
                L"run as retained qualification evidence.",
                DF_GUI_TITLE,
                MB_OK | MB_ICONWARNING);
        } else if (verified) {
            df_gui_set_status(
                L"COMPLETE — WRITE, FLUSH, AND READBACK VERIFIED",
                DF_GUI_STATUS_COMPLETE);
            SetWindowTextW(
                g_gui.progress_text,
                L"SUCCESS_VERIFIED — JSON EVIDENCE SAVED");
            MessageBoxW(
                g_gui.window,
                L"WRITE COMPLETE.\r\n\r\n"
                L"Flush passed, readback verification passed, "
                L"and JSON evidence was written.",
                DF_GUI_TITLE,
                MB_OK | MB_ICONINFORMATION);
        } else {
            df_gui_set_status(
                L"COMPLETE — WRITE FLUSHED BUT NOT PROVEN",
                DF_GUI_STATUS_WARNING);
            SetWindowTextW(
                g_gui.progress_text,
                L"SUCCESS_UNVERIFIED — MEDIA WAS NOT READ BACK");
            MessageBoxW(
                g_gui.window,
                L"WRITE COMPLETE BUT UNVERIFIED.\r\n\r\n"
                L"The media was not read back. "
                L"This result is not proof.",
                DF_GUI_TITLE,
                MB_OK | MB_ICONWARNING);
        }
    } else {
        df_gui_set_progress_mode(false, PBST_ERROR, 0);
        df_gui_set_status(
            L"FAILED — READ THE ERROR AND RETAIN THE EVIDENCE",
            DF_GUI_STATUS_ERROR);
        SetWindowTextW(g_gui.progress_text,
                       L"NON-SUCCESS RESULT — DO NOT ASSUME MEDIA IS VALID");
        MessageBoxW(
            g_gui.window, error_text,
            L"DEADFLASH OPERATION FAILED",
            MB_OK | MB_ICONERROR);
    }
}

static void df_gui_open_evidence_folder(void) {
    if (g_gui.evidence_folder[0] == L'\0') return;
    if ((INT_PTR)ShellExecuteW(g_gui.window, L"open",
                               g_gui.evidence_folder,
                               NULL, NULL,
                               SW_SHOWNORMAL) <= 32) {
        MessageBoxW(g_gui.window,
                    L"Could not open the evidence folder.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
    }
}

static LRESULT CALLBACK df_gui_window_proc(
    HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            g_gui.window = window;
            if (!df_gui_create_controls()) {
                df_gui_startup_trace(L"create controls",
                                     GetLastError());
                return -1;
            }
            DragAcceptFiles(window, TRUE);
            if (SetTimer(window, DF_GUI_TIMER_AUTOSCAN,
                         350u, NULL) == 0u) {
                df_gui_append_log(
                    L"[START] autoscan timer failed; press REFRESH DEVICES\r\n");
                g_gui.scanning = false;
                df_gui_update_ready(true);
            }
            return 0;

        case WM_TIMER:
            if (wparam == DF_GUI_TIMER_AUTOSCAN) {
                (void)KillTimer(window,
                                DF_GUI_TIMER_AUTOSCAN);
                if (df_gui_busy()) {
                    (void)SetTimer(window,
                                   DF_GUI_TIMER_AUTOSCAN,
                                   500u, NULL);
                } else {
                    df_gui_begin_scan();
                }
                return 0;
            }
            break;

        case WM_SIZE:
            df_gui_layout();
            return 0;

        case WM_GETMINMAXINFO:
            {
                MINMAXINFO *info = (MINMAXINFO *)lparam;
                info->ptMinTrackSize.x = 920;
                info->ptMinTrackSize.y = 720;
            }
            return 0;

        case WM_DROPFILES:
            {
                HDROP drop = (HDROP)wparam;
                wchar_t path[DF_MAX_PATH_CHARS];
                if (!df_gui_busy() &&
                    DragQueryFileW(drop, 0u, path,
                                   (UINT)(sizeof(path) /
                                          sizeof(path[0]))) != 0u) {
                    df_gui_begin_source(path);
                }
                DragFinish(drop);
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_BROWSE:
                    if (HIWORD(wparam) == BN_CLICKED) {
                        df_gui_browse();
                    }
                    return 0;
                case IDC_REFRESH:
                    if (HIWORD(wparam) == BN_CLICKED) {
                        df_gui_begin_scan();
                    }
                    return 0;
                case IDC_TARGET:
                    if (HIWORD(wparam) == CBN_SELCHANGE) {
                        df_gui_show_target();
                    }
                    return 0;
                case IDC_VERIFY:
                    if (HIWORD(wparam) == CBN_SELCHANGE) {
                        g_gui.result_visible = false;
                        df_gui_update_ready(true);
                    }
                    return 0;
                case IDC_DIRECT:
                case IDC_DISMOUNT:
                    if (HIWORD(wparam) == BN_CLICKED) {
                        g_gui.result_visible = false;
                        df_gui_update_ready(true);
                    }
                    return 0;
                case IDC_WRITE:
                    if (HIWORD(wparam) == BN_CLICKED) {
                        df_gui_begin_plan();
                    }
                    return 0;
                case IDC_ELEVATE:
                    if (HIWORD(wparam) == BN_CLICKED) {
                        df_gui_restart_elevated();
                    }
                    return 0;
                case IDC_EVIDENCE:
                    if (HIWORD(wparam) == BN_CLICKED) {
                        df_gui_open_evidence_folder();
                    }
                    return 0;
                default:
                    break;
            }
            break;

        case WM_KEYDOWN:
            if (wparam == VK_F5) {
                df_gui_begin_scan();
                return 0;
            }
            break;

        case DF_GUI_WM_PROGRESS:
            {
                df_gui_progress_event *event =
                    (df_gui_progress_event *)lparam;
                df_gui_handle_progress(event);
                if (event != NULL) {
                    (void)HeapFree(GetProcessHeap(), 0u, event);
                }
            }
            return 0;

        case DF_GUI_WM_SCAN_DONE:
            df_gui_finish_scan();
            return 0;

        case DF_GUI_WM_SOURCE_DONE:
            df_gui_finish_source();
            return 0;

        case DF_GUI_WM_PLAN_DONE:
            df_gui_finish_plan();
            return 0;

        case DF_GUI_WM_WRITE_DONE:
            df_gui_finish_write();
            return 0;

        case WM_CTLCOLORSTATIC:
            if ((HWND)lparam == g_gui.status &&
                g_gui.status_brush != NULL) {
                HDC dc = (HDC)wparam;
                SetTextColor(dc, RGB(255, 255, 255));
                SetBkColor(dc, g_gui.status_color);
                return (LRESULT)g_gui.status_brush;
            }
            break;

        case WM_CLOSE:
            if (df_gui_busy()) {
                MessageBoxW(
                    window,
                    L"DEADFLASH is still scanning, hashing, "
                    L"sealing, writing, flushing, or verifying.\r\n"
                    L"The window cannot close until the worker finishes.",
                    DF_GUI_TITLE,
                    MB_OK | MB_ICONWARNING);
                return 0;
            }
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            DragAcceptFiles(window, FALSE);
            if (g_gui.font != NULL) {
                (void)DeleteObject(g_gui.font);
            }
            if (g_gui.bold_font != NULL) {
                (void)DeleteObject(g_gui.bold_font);
            }
            if (g_gui.mono != NULL) {
                (void)DeleteObject(g_gui.mono);
            }
            if (g_gui.status_brush != NULL) {
                (void)DeleteObject(g_gui.status_brush);
            }
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance,
                    HINSTANCE previous_instance,
                    PWSTR command_line,
                    int show_command) {
    INITCOMMONCONTROLSEX controls;
    WNDCLASSEXW window_class;
    HWND window;
    MSG message;
    (void)previous_instance;
    (void)command_line;
    (void)show_command;

    ZeroMemory(&g_gui, sizeof(g_gui));
    g_gui.active_phase = DF_PROGRESS_COMPLETE;
    df_gui_startup_trace(L"entry", 0u);
    (void)SetProcessDPIAware();

    controls.dwSize = (DWORD)sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&controls)) {
        df_gui_startup_trace(
            L"InitCommonControlsEx fallback", GetLastError());
    }

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = (UINT)sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = df_gui_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    window_class.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
    window_class.hbrBackground =
        (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = DF_GUI_CLASS;

    if (RegisterClassExW(&window_class) == 0u) {
        df_gui_show_startup_error(
            L"RegisterClassExW", GetLastError());
        return 1;
    }

    window = CreateWindowExW(
        WS_EX_CONTROLPARENT | WS_EX_ACCEPTFILES,
        DF_GUI_CLASS,
        L"DEADFLASH 1.0.0 — Image Writer & Verifier",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1060, 830,
        NULL, NULL, instance, NULL);
    if (window == NULL) {
        df_gui_show_startup_error(
            L"CreateWindowExW", GetLastError());
        return 1;
    }

    ShowWindow(window, SW_SHOW);
    if (!UpdateWindow(window)) {
        const DWORD code = GetLastError();
        if (code != ERROR_SUCCESS) {
            df_gui_startup_trace(L"UpdateWindow", code);
        }
    }
    SetForegroundWindow(window);
    SetFocus(g_gui.browse);
    df_gui_startup_trace(L"window visible", 0u);

    for (;;) {
        const BOOL result =
            GetMessageW(&message, NULL, 0u, 0u);
        if (result == 0) break;
        if (result == -1) {
            df_gui_show_startup_error(
                L"GetMessageW", GetLastError());
            return 1;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return (int)message.wParam;
}
