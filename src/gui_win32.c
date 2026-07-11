#include "deadflash/report.h"

#if !defined(_WIN32)
#error "deadflash-gui is Windows-only"
#endif

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windows.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DF_GUI_CLASS_NAME L"DeadflashMainWindow"
#define DF_GUI_TITLE L"DEADFLASH 1.0.0"
#define DF_GUI_DEVICE_LIMIT 32u
#define DF_GUI_LOG_CHARS 32768u
#define DF_GUI_WM_WRITE_DONE (WM_APP + 1u)

#define IDC_SOURCE_EDIT 1001
#define IDC_SOURCE_BROWSE 1002
#define IDC_TARGET_COMBO 1003
#define IDC_TARGET_REFRESH 1004
#define IDC_TARGET_DETAILS 1005
#define IDC_VERIFY_COMBO 1006
#define IDC_DIRECT_IO 1007
#define IDC_DISMOUNT 1008
#define IDC_WRITE 1009
#define IDC_PROGRESS 1010
#define IDC_LOG 1011
#define IDC_ELEVATE 1012

typedef struct df_gui_target_entry {
    df_target_info info;
} df_gui_target_entry;

typedef struct df_gui_worker {
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
} df_gui_worker;

typedef struct df_gui_state {
    HWND window;
    HWND source_edit;
    HWND source_browse;
    HWND target_combo;
    HWND target_refresh;
    HWND target_details;
    HWND verify_combo;
    HWND direct_io;
    HWND dismount;
    HWND write_button;
    HWND progress;
    HWND log;
    HWND header_title;
    HWND header_tagline;
    HWND admin_text;
    HWND elevate_button;
    HFONT font;
    HFONT font_bold;
    HFONT font_title;
    HFONT font_mono;
    HBRUSH header_brush;
    HBRUSH window_brush;
    df_gui_target_entry targets[DF_GUI_DEVICE_LIMIT];
    unsigned target_count;
    bool elevated;
    bool writing;
    df_gui_worker worker;
} df_gui_state;

static df_gui_state g_state;

static void df_gui_set_font(HWND control, HFONT font) {
    (void)SendMessageW(control, WM_SETFONT, (WPARAM)font, (LPARAM)TRUE);
}

static bool df_gui_utf8_to_wide(const char *input, wchar_t *output, size_t output_count) {
    int converted;
    if (input == NULL || output == NULL || output_count == 0u) return false;
    output[0] = L'\0';
    converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1,
                                    output, (int)output_count);
    return converted > 0;
}

static bool df_gui_wide_to_utf8(const wchar_t *input, char *output, size_t output_count) {
    int converted;
    if (input == NULL || output == NULL || output_count == 0u) return false;
    output[0] = '\0';
    converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1,
                                    output, (int)output_count, NULL, NULL);
    return converted > 0;
}

static void df_gui_append_log(const wchar_t *text) {
    LRESULT length;
    if (g_state.log == NULL || text == NULL) return;
    length = SendMessageW(g_state.log, WM_GETTEXTLENGTH, 0, 0);
    if ((size_t)length > DF_GUI_LOG_CHARS) {
        SetWindowTextW(g_state.log, L"");
        length = 0;
    }
    (void)SendMessageW(g_state.log, EM_SETSEL, (WPARAM)length, (LPARAM)length);
    (void)SendMessageW(g_state.log, EM_REPLACESEL, (WPARAM)FALSE, (LPARAM)text);
    (void)SendMessageW(g_state.log, EM_SCROLLCARET, 0, 0);
}

static void df_gui_format_size(uint64_t bytes, wchar_t output[64]) {
    const double gib = (double)bytes / 1073741824.0;
    const double mib = (double)bytes / 1048576.0;
    if (bytes >= 1073741824ULL) {
        (void)swprintf_s(output, 64u, L"%.2f GiB", gib);
    } else {
        (void)swprintf_s(output, 64u, L"%.1f MiB", mib);
    }
}

static bool df_gui_is_elevated(void) {
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation;
    DWORD returned = 0;
    bool result = false;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    if (GetTokenInformation(token, TokenElevation, &elevation,
                            (DWORD)sizeof(elevation), &returned)) {
        result = elevation.TokenIsElevated != 0;
    }
    CloseHandle(token);
    return result;
}

static void df_gui_restart_elevated(void) {
    wchar_t executable[MAX_PATH];
    if (GetModuleFileNameW(NULL, executable, MAX_PATH) == 0u) {
        MessageBoxW(g_state.window, L"Could not resolve the executable path.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    if ((INT_PTR)ShellExecuteW(g_state.window, L"runas", executable, NULL, NULL,
                               SW_SHOWNORMAL) <= 32) {
        MessageBoxW(g_state.window, L"Windows did not grant administrator access.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    DestroyWindow(g_state.window);
}

static int df_gui_selected_target_index(void) {
    LRESULT selection = SendMessageW(g_state.target_combo, CB_GETCURSEL, 0, 0);
    LRESULT item_data;
    if (selection == CB_ERR) return -1;
    item_data = SendMessageW(g_state.target_combo, CB_GETITEMDATA,
                             (WPARAM)selection, 0);
    if (item_data == CB_ERR || item_data < 0 ||
        (unsigned)item_data >= g_state.target_count) {
        return -1;
    }
    return (int)item_data;
}

static void df_gui_update_write_enabled(void) {
    wchar_t source[DF_MAX_PATH_CHARS];
    const int target_index = df_gui_selected_target_index();
    bool enabled = !g_state.writing && g_state.elevated && target_index >= 0;
    GetWindowTextW(g_state.source_edit, source, (int)(sizeof(source) / sizeof(source[0])));
    if (source[0] == L'\0') enabled = false;
    if (target_index >= 0) {
        const df_target_info *target = &g_state.targets[(unsigned)target_index].info;
        if (target->system_disk || target->read_only || target->size_bytes == 0u) enabled = false;
        if (target->mounted &&
            SendMessageW(g_state.dismount, BM_GETCHECK, 0, 0) != BST_CHECKED) {
            enabled = false;
        }
    }
    EnableWindow(g_state.write_button, enabled ? TRUE : FALSE);
}

static void df_gui_update_target_details(void) {
    const int target_index = df_gui_selected_target_index();
    wchar_t details[2048];
    wchar_t product[256];
    wchar_t vendor[128];
    wchar_t bus[64];
    wchar_t size[64];
    wchar_t path_wide[DF_MAX_PATH_CHARS];
    wchar_t token_wide[64];
    const wchar_t *identity;
    if (target_index < 0) {
        SetWindowTextW(g_state.target_details,
                       L"NO TARGET SELECTED\r\n\r\nREFRESH THE DEVICE LIST.");
        df_gui_update_write_enabled();
        return;
    }
    {
        const df_target_info *target = &g_state.targets[(unsigned)target_index].info;
        (void)df_gui_utf8_to_wide(target->product[0] != '\0' ? target->product : "UNKNOWN",
                                  product, sizeof(product) / sizeof(product[0]));
        (void)df_gui_utf8_to_wide(target->vendor[0] != '\0' ? target->vendor : "-",
                                  vendor, sizeof(vendor) / sizeof(vendor[0]));
        (void)df_gui_utf8_to_wide(target->bus_type[0] != '\0' ? target->bus_type : "-",
                                  bus, sizeof(bus) / sizeof(bus[0]));
        df_gui_format_size(target->size_bytes, size);
        (void)df_gui_utf8_to_wide(target->path, path_wide, sizeof(path_wide) / sizeof(path_wide[0]));
        (void)df_gui_utf8_to_wide(target->token, token_wide, sizeof(token_wide) / sizeof(token_wide[0]));
        identity = target->serial_bound ? L"SERIAL_BOUND" :
                   (target->descriptor_present ? L"DESCRIPTOR_BOUND" : L"GEOMETRY_ONLY");
        (void)swprintf_s(details, sizeof(details) / sizeof(details[0]),
                         L"PATH       %ls\r\n"
                         L"MODEL      %ls %ls\r\n"
                         L"CAPACITY   %ls\r\n"
                         L"BUS        %ls\r\n"
                         L"SECTORS    %u logical / %u physical\r\n"
                         L"IDENTITY   %ls\r\n"
                         L"TOKEN      %ls\r\n"
                         L"STATE      removable=%ls  mounted=%ls  read-only=%ls  system=%ls",
                         path_wide, vendor, product, size, bus,
                         target->logical_sector_size, target->physical_sector_size,
                         identity, token_wide,
                         target->removable ? L"YES" : L"NO",
                         target->mounted ? L"YES" : L"NO",
                         target->read_only ? L"YES" : L"NO",
                         target->system_disk ? L"BLOCKED" : L"NO");
    }
    SetWindowTextW(g_state.target_details, details);
    df_gui_update_write_enabled();
}

static void df_gui_refresh_targets(void) {
    unsigned disk;
    int first_safe = -1;
    (void)SendMessageW(g_state.target_combo, CB_RESETCONTENT, 0, 0);
    g_state.target_count = 0u;
    SetWindowTextW(g_state.target_details, L"SCANNING PHYSICAL DRIVES...");
    EnableWindow(g_state.target_refresh, FALSE);

    for (disk = 0u; disk < DF_GUI_DEVICE_LIMIT; ++disk) {
        char path[64];
        df_target_info info;
        df_error error;
        wchar_t product[256];
        wchar_t size[64];
        wchar_t label[512];
        LRESULT item;
        (void)snprintf(path, sizeof(path), "\\\\.\\PhysicalDrive%u", disk);
        if (df_inspect_target(path, &info, &error) != DF_OK) continue;
        if (info.kind != DF_TARGET_BLOCK_DEVICE || info.size_bytes == 0u) continue;
        if (g_state.target_count >= DF_GUI_DEVICE_LIMIT) break;
        g_state.targets[g_state.target_count].info = info;
        (void)df_gui_utf8_to_wide(info.product[0] != '\0' ? info.product : "UNKNOWN DEVICE",
                                  product, sizeof(product) / sizeof(product[0]));
        df_gui_format_size(info.size_bytes, size);
        (void)swprintf_s(label, sizeof(label) / sizeof(label[0]),
                         L"PhysicalDrive%u  |  %ls  |  %ls%s%s",
                         disk, product, size,
                         info.system_disk ? L"  |  SYSTEM - BLOCKED" : L"",
                         info.read_only ? L"  |  READ ONLY" : L"");
        item = SendMessageW(g_state.target_combo, CB_ADDSTRING, 0, (LPARAM)label);
        if (item != CB_ERR && item != CB_ERRSPACE) {
            (void)SendMessageW(g_state.target_combo, CB_SETITEMDATA,
                               (WPARAM)item, (LPARAM)g_state.target_count);
            if (first_safe < 0 && !info.system_disk && !info.read_only) {
                first_safe = (int)item;
            }
            ++g_state.target_count;
        }
    }

    EnableWindow(g_state.target_refresh, TRUE);
    if (g_state.target_count == 0u) {
        SetWindowTextW(g_state.target_details,
                       L"NO PHYSICAL DRIVE WAS FOUND.\r\n\r\n"
                       L"CONNECT A USB DEVICE AND PRESS REFRESH.");
        df_gui_append_log(L"[SCAN] no physical drives found\r\n");
    } else {
        const int selection = first_safe >= 0 ? first_safe : 0;
        (void)SendMessageW(g_state.target_combo, CB_SETCURSEL, (WPARAM)selection, 0);
        df_gui_update_target_details();
        {
            wchar_t line[128];
            (void)swprintf_s(line, sizeof(line) / sizeof(line[0]),
                             L"[SCAN] %u physical drive(s) found\r\n",
                             g_state.target_count);
            df_gui_append_log(line);
        }
    }
    df_gui_update_write_enabled();
}

static void df_gui_browse_source(void) {
    OPENFILENAMEW dialog;
    wchar_t path[DF_MAX_PATH_CHARS];
    ZeroMemory(&dialog, sizeof(dialog));
    ZeroMemory(path, sizeof(path));
    dialog.lStructSize = (DWORD)sizeof(dialog);
    dialog.hwndOwner = g_state.window;
    dialog.lpstrFilter = L"Disk images (*.img;*.iso;*.bin)\0*.img;*.iso;*.bin\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Select source image";
    if (GetOpenFileNameW(&dialog)) {
        SetWindowTextW(g_state.source_edit, path);
        df_gui_append_log(L"[SOURCE] image selected\r\n");
        df_gui_update_write_enabled();
    }
}

static bool df_gui_make_report_path(char output[DF_MAX_PATH_CHARS]) {
    SYSTEMTIME now;
    wchar_t directory[DF_MAX_PATH_CHARS];
    wchar_t report[DF_MAX_PATH_CHARS];
    DWORD length = GetCurrentDirectoryW((DWORD)(sizeof(directory) / sizeof(directory[0])), directory);
    if (length == 0u || length >= (DWORD)(sizeof(directory) / sizeof(directory[0]))) return false;
    GetLocalTime(&now);
    if (swprintf_s(report, sizeof(report) / sizeof(report[0]),
                   L"%ls\\deadflash-evidence-%04u%02u%02u-%02u%02u%02u.json",
                   directory, (unsigned)now.wYear, (unsigned)now.wMonth,
                   (unsigned)now.wDay, (unsigned)now.wHour,
                   (unsigned)now.wMinute, (unsigned)now.wSecond) < 0) {
        return false;
    }
    return df_gui_wide_to_utf8(report, output, DF_MAX_PATH_CHARS);
}

static DWORD WINAPI df_gui_write_thread(LPVOID parameter) {
    df_gui_worker *worker = (df_gui_worker *)parameter;
    df_report_context context;
    memset(&worker->result, 0, sizeof(worker->result));
    df_error_clear(&worker->error);
    df_error_clear(&worker->report_error);
    worker->options.confirmation_token = worker->confirmation_token;
    worker->status = df_write_image(worker->source_path, worker->target.path,
                                    &worker->options, &worker->target,
                                    &worker->result, &worker->error);
    memset(&context, 0, sizeof(context));
    context.operation = "write";
    context.source_path = worker->source_path;
    context.target_path = worker->target.path;
    context.target = &worker->target;
    context.write_options = &worker->options;
    context.write_result = &worker->result;
    context.status = worker->status;
    context.error = &worker->error;
    worker->report_written =
        df_write_json_report(worker->report_path, &context, &worker->report_error) == DF_OK;
    (void)PostMessageW(worker->owner, DF_GUI_WM_WRITE_DONE, 0, 0);
    return 0u;
}

static void df_gui_begin_write(void) {
    const int target_index = df_gui_selected_target_index();
    wchar_t source_wide[DF_MAX_PATH_CHARS];
    wchar_t confirmation[1024];
    wchar_t product[256];
    wchar_t size[64];
    wchar_t target_path_wide[DF_MAX_PATH_CHARS];
    HANDLE thread;
    int verify_selection;

    if (g_state.writing || target_index < 0) return;
    GetWindowTextW(g_state.source_edit, source_wide,
                   (int)(sizeof(source_wide) / sizeof(source_wide[0])));
    if (source_wide[0] == L'\0') return;
    if (!g_state.elevated) {
        MessageBoxW(g_state.window, L"Administrator access is required to write a physical drive.",
                    DF_GUI_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }

    g_state.worker.owner = g_state.window;
    g_state.worker.target = g_state.targets[(unsigned)target_index].info;
    (void)strcpy_s(g_state.worker.confirmation_token,
                   sizeof(g_state.worker.confirmation_token),
                   g_state.worker.target.token);
    if (g_state.worker.target.system_disk) {
        MessageBoxW(g_state.window, L"The Windows system disk is blocked by the GUI and core.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    if (g_state.worker.target.read_only) {
        MessageBoxW(g_state.window, L"The selected target reports read-only status.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    if (g_state.worker.target.mounted &&
        SendMessageW(g_state.dismount, BM_GETCHECK, 0, 0) != BST_CHECKED) {
        MessageBoxW(g_state.window,
                    L"The target is mounted. Enable LOCK + DISMOUNT before writing.",
                    DF_GUI_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    if (!df_gui_wide_to_utf8(source_wide, g_state.worker.source_path,
                             sizeof(g_state.worker.source_path))) {
        MessageBoxW(g_state.window, L"The source path could not be converted to UTF-8.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    if (!df_gui_make_report_path(g_state.worker.report_path)) {
        MessageBoxW(g_state.window, L"Could not create the evidence path.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    (void)df_gui_utf8_to_wide(g_state.worker.target.product[0] != '\0' ?
                              g_state.worker.target.product : "UNKNOWN DEVICE",
                              product, sizeof(product) / sizeof(product[0]));
    df_gui_format_size(g_state.worker.target.size_bytes, size);
    (void)df_gui_utf8_to_wide(g_state.worker.target.path, target_path_wide,
                              sizeof(target_path_wide) / sizeof(target_path_wide[0]));
    (void)swprintf_s(confirmation, sizeof(confirmation) / sizeof(confirmation[0]),
                     L"DESTROY ALL DATA ON THIS TARGET?\r\n\r\n"
                     L"%ls\r\n%ls\r\n%ls\r\n\r\n"
                     L"The source image will be written, flushed, and verified.\r\n"
                     L"This operation cannot be undone.",
                     product, size, target_path_wide);
    if (MessageBoxW(g_state.window, confirmation, L"DEADFLASH DESTRUCTIVE CONFIRMATION",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        df_gui_append_log(L"[CANCEL] destructive confirmation rejected\r\n");
        return;
    }

    memset(&g_state.worker.options, 0, sizeof(g_state.worker.options));
    g_state.worker.options.buffer_size = DF_DEFAULT_BUFFER_SIZE;
    g_state.worker.options.write_retries = 4u;
    g_state.worker.options.sample_count = 64u;
    g_state.worker.options.allow_device = true;
    g_state.worker.options.force_mounted =
        SendMessageW(g_state.dismount, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_state.worker.options.force_system_disk = false;
    g_state.worker.options.direct_io =
        SendMessageW(g_state.direct_io, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g_state.worker.options.truncate_regular_file = true;
    verify_selection = (int)SendMessageW(g_state.verify_combo, CB_GETCURSEL, 0, 0);
    if (verify_selection == 1) g_state.worker.options.verify_mode = DF_VERIFY_SAMPLE;
    else if (verify_selection == 2) g_state.worker.options.verify_mode = DF_VERIFY_NONE;
    else g_state.worker.options.verify_mode = DF_VERIFY_FULL;

    g_state.writing = true;
    EnableWindow(g_state.source_browse, FALSE);
    EnableWindow(g_state.target_combo, FALSE);
    EnableWindow(g_state.target_refresh, FALSE);
    EnableWindow(g_state.verify_combo, FALSE);
    EnableWindow(g_state.direct_io, FALSE);
    EnableWindow(g_state.dismount, FALSE);
    EnableWindow(g_state.write_button, FALSE);
    (void)SendMessageW(g_state.progress, PBM_SETMARQUEE, (WPARAM)TRUE, 40);
    df_gui_append_log(L"\r\n[WRITE] started; progress is indeterminate until the core exposes byte callbacks\r\n");
    df_gui_append_log(L"[WRITE] hashing source, writing exact bytes, flushing, then verifying\r\n");

    thread = CreateThread(NULL, 0u, df_gui_write_thread, &g_state.worker, 0u, NULL);
    if (thread == NULL) {
        g_state.writing = false;
        (void)SendMessageW(g_state.progress, PBM_SETMARQUEE, (WPARAM)FALSE, 0);
        MessageBoxW(g_state.window, L"Could not start the write worker thread.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        EnableWindow(g_state.source_browse, TRUE);
        EnableWindow(g_state.target_combo, TRUE);
        EnableWindow(g_state.target_refresh, TRUE);
        EnableWindow(g_state.verify_combo, TRUE);
        EnableWindow(g_state.direct_io, TRUE);
        EnableWindow(g_state.dismount, TRUE);
        df_gui_update_write_enabled();
        return;
    }
    CloseHandle(thread);
}

static void df_gui_finish_write(void) {
    wchar_t line[2048];
    wchar_t report[DF_MAX_PATH_CHARS];
    g_state.writing = false;
    (void)SendMessageW(g_state.progress, PBM_SETMARQUEE, (WPARAM)FALSE, 0);
    (void)SendMessageW(g_state.progress, PBM_SETPOS,
                       (WPARAM)(g_state.worker.status == DF_OK ? 100 : 0), 0);

    {
        wchar_t final_state[96];
        wchar_t status_name[96];
        (void)df_gui_utf8_to_wide(g_state.worker.result.final_state, final_state,
                                  sizeof(final_state) / sizeof(final_state[0]));
        (void)df_gui_utf8_to_wide(df_status_name(g_state.worker.status), status_name,
                                  sizeof(status_name) / sizeof(status_name[0]));
        (void)swprintf_s(line, sizeof(line) / sizeof(line[0]),
                     L"[RESULT] state=%ls  status=%ls\r\n"
                     L"[RESULT] written=%llu  verified=%llu\r\n"
                     L"[TIMING] hash=%.3f ms  write=%.3f ms  flush=%.3f ms  verify=%.3f ms  total=%.3f ms\r\n",
                     final_state,
                     status_name,
                     (unsigned long long)g_state.worker.result.bytes_written,
                     (unsigned long long)g_state.worker.result.bytes_verified,
                     g_state.worker.result.source_hash_ms,
                     g_state.worker.result.write_ms,
                     g_state.worker.result.flush_ms,
                     g_state.worker.result.verify_ms,
                     g_state.worker.result.total_ms);
    }
    df_gui_append_log(line);
    if (g_state.worker.error.message[0] != '\0') {
        (void)df_gui_utf8_to_wide(g_state.worker.error.message, line,
                                  sizeof(line) / sizeof(line[0]));
        df_gui_append_log(L"[ERROR] ");
        df_gui_append_log(line);
        df_gui_append_log(L"\r\n");
    }
    if (df_gui_utf8_to_wide(g_state.worker.report_path, report,
                            sizeof(report) / sizeof(report[0]))) {
        df_gui_append_log(g_state.worker.report_written ? L"[EVIDENCE] " : L"[EVIDENCE ERROR] ");
        df_gui_append_log(report);
        df_gui_append_log(L"\r\n");
    }

    EnableWindow(g_state.source_browse, TRUE);
    EnableWindow(g_state.target_combo, TRUE);
    EnableWindow(g_state.target_refresh, TRUE);
    EnableWindow(g_state.verify_combo, TRUE);
    EnableWindow(g_state.direct_io, TRUE);
    EnableWindow(g_state.dismount, TRUE);
    df_gui_update_write_enabled();

    if (g_state.worker.status == DF_OK) {
        MessageBoxW(g_state.window,
                    L"WRITE COMPLETE. FLUSH AND VERIFICATION PASSED.\r\n\r\nJSON evidence was retained.",
                    DF_GUI_TITLE, MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_state.window,
                    L"WRITE DID NOT COMPLETE SUCCESSFULLY.\r\n\r\nRead the operation log and retained JSON evidence.",
                    DF_GUI_TITLE, MB_OK | MB_ICONERROR);
    }
    df_gui_refresh_targets();
}

static HWND df_gui_create_control(DWORD ex_style, const wchar_t *class_name,
                                  const wchar_t *text, DWORD style,
                                  int x, int y, int width, int height,
                                  int id) {
    return CreateWindowExW(ex_style, class_name, text, style,
                           x, y, width, height, g_state.window,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}

static void df_gui_create_controls(void) {
    LOGFONTW log_font;
    HWND control;
    HFONT default_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    GetObjectW(default_font, (int)sizeof(log_font), &log_font);
    g_state.font = CreateFontIndirectW(&log_font);
    log_font.lfWeight = FW_BOLD;
    g_state.font_bold = CreateFontIndirectW(&log_font);
    log_font.lfHeight = -26;
    log_font.lfWeight = FW_BOLD;
    (void)wcscpy_s(log_font.lfFaceName, LF_FACESIZE, L"Segoe UI");
    g_state.font_title = CreateFontIndirectW(&log_font);
    log_font.lfHeight = -15;
    log_font.lfWeight = FW_NORMAL;
    (void)wcscpy_s(log_font.lfFaceName, LF_FACESIZE, L"Consolas");
    g_state.font_mono = CreateFontIndirectW(&log_font);

    g_state.header_title = df_gui_create_control(0, L"STATIC", L"DEADFLASH 1.0.0",
                                                  WS_CHILD | WS_VISIBLE,
                                                  24, 14, 360, 32, 0);
    df_gui_set_font(g_state.header_title, g_state.font_title);
    g_state.header_tagline = df_gui_create_control(0, L"STATIC", L"WRITE THE IMAGE. VERIFY THE TRUTH.",
                                                    WS_CHILD | WS_VISIBLE,
                                                    26, 45, 420, 20, 0);
    df_gui_set_font(g_state.header_tagline, g_state.font);
    g_state.admin_text = df_gui_create_control(0, L"STATIC", L"",
                                                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                                610, 18, 265, 20, 0);
    df_gui_set_font(g_state.admin_text, g_state.font_bold);
    g_state.elevate_button = df_gui_create_control(0, L"BUTTON", L"RUN AS ADMIN",
                                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                    735, 42, 140, 25, IDC_ELEVATE);
    df_gui_set_font(g_state.elevate_button, g_state.font_bold);

    control = df_gui_create_control(0, L"STATIC", L"SOURCE IMAGE",
                                    WS_CHILD | WS_VISIBLE,
                                    24, 90, 160, 20, 0);
    df_gui_set_font(control, g_state.font_bold);
    g_state.source_edit = df_gui_create_control(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                                 24, 113, 724, 28, IDC_SOURCE_EDIT);
    df_gui_set_font(g_state.source_edit, g_state.font);
    g_state.source_browse = df_gui_create_control(0, L"BUTTON", L"SELECT IMAGE",
                                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                   760, 112, 115, 30, IDC_SOURCE_BROWSE);
    df_gui_set_font(g_state.source_browse, g_state.font_bold);

    control = df_gui_create_control(0, L"STATIC", L"TARGET DEVICE",
                                    WS_CHILD | WS_VISIBLE,
                                    24, 157, 160, 20, 0);
    df_gui_set_font(control, g_state.font_bold);
    g_state.target_combo = df_gui_create_control(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
                                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                                  CBS_DROPDOWNLIST,
                                                  24, 180, 724, 300, IDC_TARGET_COMBO);
    df_gui_set_font(g_state.target_combo, g_state.font);
    g_state.target_refresh = df_gui_create_control(0, L"BUTTON", L"REFRESH",
                                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                    760, 179, 115, 30, IDC_TARGET_REFRESH);
    df_gui_set_font(g_state.target_refresh, g_state.font_bold);

    control = df_gui_create_control(0, L"STATIC", L"DEVICE OVERVIEW",
                                    WS_CHILD | WS_VISIBLE,
                                    24, 222, 180, 20, 0);
    df_gui_set_font(control, g_state.font_bold);
    g_state.target_details = df_gui_create_control(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                                                    ES_READONLY | ES_AUTOVSCROLL,
                                                    24, 245, 851, 126, IDC_TARGET_DETAILS);
    df_gui_set_font(g_state.target_details, g_state.font_mono);

    control = df_gui_create_control(0, L"STATIC", L"VERIFY",
                                    WS_CHILD | WS_VISIBLE,
                                    24, 389, 80, 20, 0);
    df_gui_set_font(control, g_state.font_bold);
    g_state.verify_combo = df_gui_create_control(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
                                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                                  24, 412, 210, 200, IDC_VERIFY_COMBO);
    df_gui_set_font(g_state.verify_combo, g_state.font);
    (void)SendMessageW(g_state.verify_combo, CB_ADDSTRING, 0,
                       (LPARAM)L"FULL READBACK (RECOMMENDED)");
    (void)SendMessageW(g_state.verify_combo, CB_ADDSTRING, 0,
                       (LPARAM)L"SAMPLED READBACK");
    (void)SendMessageW(g_state.verify_combo, CB_ADDSTRING, 0,
                       (LPARAM)L"NONE (NOT PROVEN)");
    (void)SendMessageW(g_state.verify_combo, CB_SETCURSEL, 0, 0);

    g_state.direct_io = df_gui_create_control(0, L"BUTTON", L"DIRECT I/O",
                                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                               270, 412, 150, 26, IDC_DIRECT_IO);
    df_gui_set_font(g_state.direct_io, g_state.font);
    g_state.dismount = df_gui_create_control(0, L"BUTTON", L"LOCK + DISMOUNT MOUNTED VOLUMES",
                                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                              430, 412, 320, 26, IDC_DISMOUNT);
    df_gui_set_font(g_state.dismount, g_state.font);
    (void)SendMessageW(g_state.dismount, BM_SETCHECK, BST_CHECKED, 0);

    g_state.write_button = df_gui_create_control(0, L"BUTTON", L"WRITE + VERIFY",
                                                  WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                                  760, 400, 115, 42, IDC_WRITE);
    df_gui_set_font(g_state.write_button, g_state.font_bold);
    g_state.progress = df_gui_create_control(0, PROGRESS_CLASSW, L"",
                                              WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                                              24, 456, 851, 18, IDC_PROGRESS);
    (void)SendMessageW(g_state.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    control = df_gui_create_control(0, L"STATIC", L"OPERATION LOG",
                                    WS_CHILD | WS_VISIBLE,
                                    24, 490, 180, 20, 0);
    df_gui_set_font(control, g_state.font_bold);
    g_state.log = df_gui_create_control(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                         WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                         ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                         24, 513, 851, 143, IDC_LOG);
    df_gui_set_font(g_state.log, g_state.font_mono);

    g_state.elevated = df_gui_is_elevated();
    SetWindowTextW(g_state.admin_text,
                   g_state.elevated ? L"ADMINISTRATOR: YES" : L"ADMINISTRATOR: NO");
    ShowWindow(g_state.elevate_button, g_state.elevated ? SW_HIDE : SW_SHOW);
    df_gui_append_log(L"DEADFLASH GUI\r\n");
    df_gui_append_log(L"Native Win32 frontend. The core remains authoritative.\r\n");
    if (!g_state.elevated) {
        df_gui_append_log(L"[BLOCKED] administrator access is required for physical-device writes\r\n");
    }
    df_gui_refresh_targets();
}

static LRESULT CALLBACK df_gui_window_proc(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            g_state.window = window;
            df_gui_create_controls();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_SOURCE_BROWSE:
                    if (HIWORD(wparam) == BN_CLICKED) df_gui_browse_source();
                    return 0;
                case IDC_TARGET_REFRESH:
                    if (HIWORD(wparam) == BN_CLICKED) df_gui_refresh_targets();
                    return 0;
                case IDC_TARGET_COMBO:
                    if (HIWORD(wparam) == CBN_SELCHANGE) df_gui_update_target_details();
                    return 0;
                case IDC_DISMOUNT:
                case IDC_DIRECT_IO:
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
        case DF_GUI_WM_WRITE_DONE:
            df_gui_finish_write();
            return 0;
        case WM_CTLCOLORSTATIC:
            if ((HWND)lparam == g_state.admin_text) {
                SetTextColor((HDC)wparam, g_state.elevated ? RGB(190, 255, 190) : RGB(255, 180, 180));
                SetBkColor((HDC)wparam, RGB(24, 24, 24));
                return (LRESULT)g_state.header_brush;
            }
            if ((HWND)lparam == g_state.header_title ||
                (HWND)lparam == g_state.header_tagline) {
                SetTextColor((HDC)wparam, RGB(245, 245, 245));
                SetBkColor((HDC)wparam, RGB(24, 24, 24));
                return (LRESULT)g_state.header_brush;
            }
            break;
        case WM_ERASEBKGND: {
            RECT client;
            RECT header;
            HDC dc = (HDC)wparam;
            GetClientRect(window, &client);
            FillRect(dc, &client, g_state.window_brush);
            header = client;
            header.bottom = 76;
            FillRect(dc, &header, g_state.header_brush);
            return 1;
        }
        case WM_CLOSE:
            if (g_state.writing) {
                MessageBoxW(window,
                            L"A destructive write is in progress. The window cannot close yet.",
                            DF_GUI_TITLE, MB_OK | MB_ICONWARNING);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (g_state.font != NULL) DeleteObject(g_state.font);
            if (g_state.font_bold != NULL) DeleteObject(g_state.font_bold);
            if (g_state.font_title != NULL) DeleteObject(g_state.font_title);
            if (g_state.font_mono != NULL) DeleteObject(g_state.font_mono);
            if (g_state.header_brush != NULL) DeleteObject(g_state.header_brush);
            if (g_state.window_brush != NULL) DeleteObject(g_state.window_brush);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance,
                    PWSTR command_line, int show_command) {
    WNDCLASSEXW window_class;
    INITCOMMONCONTROLSEX common_controls;
    HWND window;
    MSG message;
    (void)previous_instance;
    (void)command_line;

    SetProcessDPIAware();
    memset(&g_state, 0, sizeof(g_state));
    g_state.header_brush = CreateSolidBrush(RGB(24, 24, 24));
    g_state.window_brush = CreateSolidBrush(RGB(240, 240, 240));

    common_controls.dwSize = (DWORD)sizeof(common_controls);
    common_controls.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    if (!InitCommonControlsEx(&common_controls)) return 1;

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = (UINT)sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = df_gui_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    window_class.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
    window_class.hbrBackground = NULL;
    window_class.lpszClassName = DF_GUI_CLASS_NAME;
    if (RegisterClassExW(&window_class) == 0u) return 1;

    window = CreateWindowExW(0, DF_GUI_CLASS_NAME, DF_GUI_TITLE,
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 915, 710,
                             NULL, NULL, instance, NULL);
    if (window == NULL) return 1;

    ShowWindow(window, show_command);
    UpdateWindow(window);
    for (;;) {
        const BOOL result = GetMessageW(&message, NULL, 0, 0);
        if (result == 0) break;
        if (result == -1) return 1;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
