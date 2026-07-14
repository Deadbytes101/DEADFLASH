#if !defined(_WIN32)
#error "deadflash-gui is Windows-only"
#endif

#include "deadflash/clean.h"

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <stddef.h>
#include <wchar.h>

#define DF_GUI_IDC_CLEAN 1090
#define DF_GUI_WM_CLEAN_DONE (WM_APP + 80u)

typedef struct df_gui_clean_job {
    HWND owner;
    char target_path[DF_MAX_PATH_CHARS];
    char confirmation_token[DF_TOKEN_HEX_CHARS + 1u];
    char report_path[DF_MAX_PATH_CHARS];
    df_clean_result result;
    df_error error;
    df_error report_error;
    df_status status;
    bool report_written;
} df_gui_clean_job;

static WNDPROC g_df_gui_original_window_proc = NULL;
static HWND g_df_gui_clean_button = NULL;
static bool g_df_gui_clean_running = false;
static df_gui_clean_job g_df_gui_clean_job;

static ATOM WINAPI df_gui_register_class_ex_w(
    const WNDCLASSEXW *window_class);
static HWND WINAPI df_gui_create_window_ex_w(
    DWORD extended_style,
    LPCWSTR class_name,
    LPCWSTR window_name,
    DWORD style,
    int x, int y, int width, int height,
    HWND parent, HMENU menu,
    HINSTANCE instance, LPVOID parameter);
static BOOL df_gui_init_common_controls(
    const INITCOMMONCONTROLSEX *requested);
static LRESULT WINAPI df_gui_send_message_w(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam);

#define RegisterClassExW df_gui_register_class_ex_w
#define CreateWindowExW df_gui_create_window_ex_w
#define InitCommonControlsEx df_gui_init_common_controls
#define SendMessageW df_gui_send_message_w
#include "gui_win32_final.c"
#undef SendMessageW
#undef InitCommonControlsEx
#undef CreateWindowExW
#undef RegisterClassExW

static BOOL df_gui_class_name_equals(
    LPCWSTR class_name, LPCWSTR expected) {
    if (class_name == NULL || expected == NULL ||
        IS_INTRESOURCE(class_name)) {
        return FALSE;
    }
    return lstrcmpiW(class_name, expected) == 0 ? TRUE : FALSE;
}

static void df_gui_clean_layout_button(void) {
    RECT evidence;
    POINT points[2];
    int y;
    int height;

    if (g_df_gui_clean_button == NULL ||
        g_gui.evidence_button == NULL ||
        g_gui.window == NULL) return;
    if (!GetWindowRect(g_gui.evidence_button, &evidence)) return;
    points[0].x = evidence.left;
    points[0].y = evidence.top;
    points[1].x = evidence.right;
    points[1].y = evidence.bottom;
    if (MapWindowPoints(HWND_DESKTOP, g_gui.window,
                        points, 2u) == 0 && GetLastError() != 0u) {
        return;
    }
    y = points[0].y;
    height = points[1].y - points[0].y;
    if (height < 24) height = 30;
    (void)MoveWindow(g_df_gui_clean_button,
                     20, y, 180, height, TRUE);
}

static bool df_gui_clean_target_allowed(
    const df_target_info *target) {
    return target != NULL &&
           target->kind == DF_TARGET_BLOCK_DEVICE &&
           target->removable &&
           !target->read_only &&
           !target->system_disk &&
           strcmp(target->bus_type, "windows:7") == 0;
}

static void df_gui_clean_update_enabled(void) {
    const int selected = df_gui_selected_target();
    const df_target_info *target = NULL;
    bool enabled;

    if (g_df_gui_clean_button == NULL) return;
    if (selected >= 0) {
        target = &g_gui.targets[(unsigned)selected];
    }
    enabled = g_gui.elevated && !g_df_gui_clean_running &&
              !df_gui_busy() &&
              df_gui_clean_target_allowed(target);
    EnableWindow(g_df_gui_clean_button,
                 enabled ? TRUE : FALSE);
}

static bool df_gui_clean_confirm(
    const df_target_info *target) {
    wchar_t product[256];
    wchar_t path[DF_MAX_PATH_CHARS];
    wchar_t size[64];
    wchar_t token[64];
    wchar_t message[2048];

    if (target == NULL) return false;
    (void)df_gui_utf8_to_wide(
        target->product[0] != '\0' ?
            target->product : "UNKNOWN DEVICE",
        product, sizeof(product) / sizeof(product[0]));
    (void)df_gui_utf8_to_wide(
        target->path, path,
        sizeof(path) / sizeof(path[0]));
    (void)df_gui_utf8_to_wide(
        target->token, token,
        sizeof(token) / sizeof(token[0]));
    df_gui_format_size(target->size_bytes, size);

    (void)swprintf_s(
        message, sizeof(message) / sizeof(message[0]),
        L"CLEAN THE COMPLETE PHYSICAL USB?\r\n\r\n"
        L"DEVICE    %ls\r\n"
        L"PATH      %ls\r\n"
        L"CAPACITY  %ls\r\n"
        L"TOKEN     %ls\r\n\r\n"
        L"CLEAN DISK removes the entire MBR/GPT partition layout.\r\n"
        L"It does not run CLEAN ALL and does not zero-fill the disk.\r\n"
        L"Every partition and volume on this USB will disappear.",
        product, path, size, token);
    if (MessageBoxW(g_gui.window, message,
                    L"CLEAN DISK — DESTRUCTIVE ACTION",
                    MB_YESNO | MB_ICONWARNING |
                        MB_DEFBUTTON2 | MB_TOPMOST) != IDYES) {
        return false;
    }

    (void)swprintf_s(
        message, sizeof(message) / sizeof(message[0]),
        L"FINAL CLEAN CONFIRMATION\r\n\r\n"
        L"Target token: %ls\r\n"
        L"Target path:  %ls\r\n\r\n"
        L"DEADFLASH will revalidate this identity, lock and dismount "
        L"all target volumes, delete the drive layout, then require "
        L"RAW + zero partitions + zero mounts.\r\n\r\n"
        L"Continue?",
        token, path);
    return MessageBoxW(g_gui.window, message,
                       L"FINAL CLEAN CONFIRMATION",
                       MB_YESNO | MB_ICONWARNING |
                           MB_DEFBUTTON2 | MB_TOPMOST) == IDYES;
}

static DWORD WINAPI df_gui_clean_thread(LPVOID parameter) {
    df_gui_clean_job *job = (df_gui_clean_job *)parameter;

    memset(&job->result, 0, sizeof(job->result));
    df_error_clear(&job->error);
    df_error_clear(&job->report_error);
    job->status = df_clean_disk(job->target_path,
                                job->confirmation_token,
                                &job->result,
                                &job->error);
    job->report_written =
        df_write_clean_json_report(job->report_path,
                                   job->target_path,
                                   &job->result,
                                   job->status,
                                   &job->error,
                                   &job->report_error) == DF_OK;
    (void)PostMessageW(job->owner,
                       DF_GUI_WM_CLEAN_DONE, 0u, 0);
    return 0u;
}

static void df_gui_begin_clean(void) {
    const int selected = df_gui_selected_target();
    const df_target_info *target;
    HANDLE thread;

    if (selected < 0 || g_df_gui_clean_running ||
        df_gui_busy() || !g_gui.elevated) return;
    target = &g_gui.targets[(unsigned)selected];
    if (!df_gui_clean_target_allowed(target)) {
        MessageBoxW(
            g_gui.window,
            L"CLEAN DISK is restricted to removable USB physical disks.\r\n"
            L"System disks, internal disks, program disks, and read-only "
            L"targets are blocked.",
            DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    if (!df_gui_clean_confirm(target)) {
        df_gui_append_log(
            L"[CLEAN] cancelled before media change\r\n");
        df_gui_set_status(
            L"CLEAN CANCELLED — NO MEDIA CHANGED",
            DF_GUI_STATUS_IDLE);
        return;
    }

    memset(&g_df_gui_clean_job, 0,
           sizeof(g_df_gui_clean_job));
    g_df_gui_clean_job.owner = g_gui.window;
    (void)strcpy_s(g_df_gui_clean_job.target_path,
                   sizeof(g_df_gui_clean_job.target_path),
                   target->path);
    (void)strcpy_s(g_df_gui_clean_job.confirmation_token,
                   sizeof(g_df_gui_clean_job.confirmation_token),
                   target->token);
    if (!df_gui_prepare_evidence_path(
            g_df_gui_clean_job.report_path)) {
        MessageBoxW(
            g_gui.window,
            L"Could not create Documents\\DEADFLASH\\Evidence.",
            DF_GUI_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    g_df_gui_clean_running = true;
    g_gui.result_visible = false;
    (void)KillTimer(g_gui.window,
                    DF_GUI_TIMER_AUTOSCAN);
    df_gui_enable_operation_controls(false);
    EnableWindow(g_gui.write_button, FALSE);
    EnableWindow(g_df_gui_clean_button, FALSE);
    df_gui_set_status(
        L"CLEANING DISK — REVALIDATING TARGET",
        DF_GUI_STATUS_BUSY);
    df_gui_set_progress_mode(true, PBST_PAUSED, 0);
    SetWindowTextW(
        g_gui.progress_text,
        L"IDENTITY CHECK + LOCK + DISMOUNT + CLEAN + VERIFY");
    df_gui_append_log(
        L"[CLEAN] confirmation accepted; worker started\r\n");

    thread = CreateThread(NULL, 0u,
                          df_gui_clean_thread,
                          &g_df_gui_clean_job,
                          0u, NULL);
    if (thread == NULL) {
        g_df_gui_clean_running = false;
        df_gui_enable_operation_controls(true);
        df_gui_clean_update_enabled();
        MessageBoxW(g_gui.window,
                    L"Could not start the clean worker.",
                    DF_GUI_TITLE,
                    MB_OK | MB_ICONERROR);
        df_gui_update_ready(true);
        return;
    }
    CloseHandle(thread);
}

static void df_gui_finish_clean(void) {
    df_gui_clean_job *job = &g_df_gui_clean_job;
    wchar_t state[96];
    wchar_t status[96];
    wchar_t error_text[DF_MAX_ERROR_MESSAGE];
    wchar_t report[DF_MAX_PATH_CHARS];
    wchar_t line[2048];

    g_df_gui_clean_running = false;
    g_gui.result_visible = true;
    df_gui_enable_operation_controls(true);

    (void)df_gui_utf8_to_wide(job->result.final_state,
                              state,
                              sizeof(state) / sizeof(state[0]));
    (void)df_gui_utf8_to_wide(df_status_name(job->status),
                              status,
                              sizeof(status) / sizeof(status[0]));
    (void)df_gui_utf8_to_wide(
        job->error.message[0] != '\0' ?
            job->error.message : "-",
        error_text,
        sizeof(error_text) / sizeof(error_text[0]));
    (void)df_gui_utf8_to_wide(job->report_path,
                              report,
                              sizeof(report) / sizeof(report[0]));

    (void)swprintf_s(
        line, sizeof(line) / sizeof(line[0]),
        L"[CLEAN RESULT] state=%ls status=%ls\r\n"
        L"[LAYOUT] before=%hs/%u after=%hs/%u\r\n"
        L"[TIMING] clean=%.3f verify=%.3f total=%.3f ms\r\n"
        L"[ERROR] %ls\r\n"
        L"[EVIDENCE] %ls%ls\r\n",
        state, status,
        df_clean_partition_style_name(job->result.before_style),
        job->result.before_partition_count,
        df_clean_partition_style_name(job->result.after_style),
        job->result.after_partition_count,
        job->result.clean_ms,
        job->result.verify_ms,
        job->result.total_ms,
        error_text,
        job->report_written ? L"" : L"FAILED: ",
        report);
    df_gui_append_log(line);

    EnableWindow(g_gui.evidence_button,
                 job->report_written ? TRUE : FALSE);
    if (job->status == DF_OK) {
        df_gui_set_progress_mode(false, PBST_NORMAL, 100);
        if (job->report_written) {
            df_gui_set_status(
                L"COMPLETE — DISK CLEAN VERIFIED RAW",
                DF_GUI_STATUS_COMPLETE);
            SetWindowTextW(
                g_gui.progress_text,
                L"SUCCESS_CLEAN_VERIFIED — JSON EVIDENCE SAVED");
            MessageBoxW(
                g_gui.window,
                L"CLEAN COMPLETE.\r\n\r\n"
                L"The partition layout was removed and the disk "
                L"verified as RAW with zero partitions and zero mounts.\r\n"
                L"JSON evidence was written.",
                DF_GUI_TITLE,
                MB_OK | MB_ICONINFORMATION);
        } else {
            df_gui_set_status(
                L"CLEAN VERIFIED — EVIDENCE FILE FAILED",
                DF_GUI_STATUS_WARNING);
            SetWindowTextW(
                g_gui.progress_text,
                L"DISK IS CLEAN — JSON EVIDENCE WAS NOT SAVED");
            MessageBoxW(
                g_gui.window,
                L"The disk clean was verified, but the JSON evidence "
                L"file could not be saved.",
                DF_GUI_TITLE,
                MB_OK | MB_ICONWARNING);
        }
        g_gui.result_visible = false;
        df_gui_begin_scan();
    } else {
        df_gui_set_progress_mode(false, PBST_ERROR, 0);
        df_gui_set_status(
            L"CLEAN FAILED — RETAIN EVIDENCE",
            DF_GUI_STATUS_ERROR);
        SetWindowTextW(
            g_gui.progress_text,
            job->result.layout_deleted ?
                L"LAYOUT MAY HAVE CHANGED — CLEAN COULD NOT BE PROVEN" :
                L"FAILED BEFORE PARTITION LAYOUT DELETION");
        MessageBoxW(g_gui.window, error_text,
                    L"CLEAN DISK FAILED",
                    MB_OK | MB_ICONERROR);
        df_gui_update_ready(false);
        df_gui_clean_update_enabled();
    }
}

static LRESULT CALLBACK df_gui_combined_window_proc(
    HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    LRESULT result;

    if (message == WM_COMMAND &&
        LOWORD(wparam) == DF_GUI_IDC_CLEAN &&
        HIWORD(wparam) == BN_CLICKED) {
        df_gui_begin_clean();
        return 0;
    }
    if (message == DF_GUI_WM_CLEAN_DONE) {
        df_gui_finish_clean();
        return 0;
    }
    if (g_df_gui_clean_running) {
        if (message == WM_CLOSE) {
            MessageBoxW(
                window,
                L"DEADFLASH is cleaning and verifying the selected disk.\r\n"
                L"The window cannot close until the worker finishes.",
                DF_GUI_TITLE,
                MB_OK | MB_ICONWARNING);
            return 0;
        }
        if (message == WM_KEYDOWN && wparam == VK_F5) {
            return 0;
        }
        if (message == WM_DROPFILES) {
            DragFinish((HDROP)wparam);
            return 0;
        }
    }

    if (g_df_gui_original_window_proc == NULL) {
        return DefWindowProcW(window, message,
                              wparam, lparam);
    }
    result = CallWindowProcW(
        g_df_gui_original_window_proc,
        window, message, wparam, lparam);

    if (message == WM_CREATE && result != -1) {
        g_df_gui_clean_button = CreateWindowExW(
            0u, L"BUTTON", L"CLEAN DISK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                BS_PUSHBUTTON,
            0, 0, 180, 30,
            window,
            (HMENU)(INT_PTR)DF_GUI_IDC_CLEAN,
            (HINSTANCE)GetWindowLongPtrW(
                window, GWLP_HINSTANCE),
            NULL);
        if (g_df_gui_clean_button != NULL) {
            (void)SetWindowTheme(g_df_gui_clean_button,
                                 L"", L"");
            df_gui_set_font(g_df_gui_clean_button,
                            g_gui.bold_font);
            df_gui_clean_layout_button();
            df_gui_clean_update_enabled();
            df_gui_append_log(
                L"[CLEAN] verified whole-disk clean engine ready\r\n");
        }
    } else if (message == WM_SIZE) {
        df_gui_clean_layout_button();
    }

    if (message == DF_GUI_WM_SCAN_DONE ||
        message == DF_GUI_WM_SOURCE_DONE ||
        message == DF_GUI_WM_PLAN_DONE ||
        message == DF_GUI_WM_WRITE_DONE ||
        message == WM_TIMER ||
        message == WM_COMMAND) {
        df_gui_clean_update_enabled();
    }
    return result;
}

static ATOM WINAPI df_gui_register_class_ex_w(
    const WNDCLASSEXW *window_class) {
    WNDCLASSEXW combined;
    if (window_class == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0u;
    }
    combined = *window_class;
    g_df_gui_original_window_proc = combined.lpfnWndProc;
    combined.lpfnWndProc = df_gui_combined_window_proc;
    return RegisterClassExW(&combined);
}

static HWND WINAPI df_gui_create_window_ex_w(
    DWORD extended_style,
    LPCWSTR class_name,
    LPCWSTR window_name,
    DWORD style,
    int x, int y, int width, int height,
    HWND parent, HMENU menu,
    HINSTANCE instance, LPVOID parameter) {
    DWORD adjusted_style = style;
    HWND window;
    const BOOL is_progress =
        df_gui_class_name_equals(class_name,
                                 PROGRESS_CLASSW);
    const BOOL use_classic_control =
        df_gui_class_name_equals(class_name, L"Button") ||
        df_gui_class_name_equals(class_name, L"Edit") ||
        df_gui_class_name_equals(class_name, L"ComboBox") ||
        is_progress;

    if (is_progress != FALSE) {
        adjusted_style &= ~(DWORD)PBS_SMOOTH;
    }
    window = CreateWindowExW(
        extended_style,
        class_name,
        window_name,
        adjusted_style,
        x, y, width, height,
        parent, menu, instance, parameter);
    if (window == NULL) return NULL;
    if (use_classic_control != FALSE) {
        (void)SetWindowTheme(window, L"", L"");
    }
    return window;
}

static BOOL df_gui_init_common_controls(
    const INITCOMMONCONTROLSEX *requested) {
    INITCOMMONCONTROLSEX progress;
    (void)requested;

    progress.dwSize = (DWORD)sizeof(progress);
    progress.dwICC = ICC_PROGRESS_CLASS;
    if (InitCommonControlsEx(&progress)) return TRUE;
    InitCommonControls();
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static LRESULT WINAPI df_gui_call_send_message_w(
    HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    return SendMessageW(window, message, wparam, lparam);
}

static void df_gui_set_progress_style(HWND window,
                                      BOOL marquee) {
    LONG_PTR style;
    LONG_PTR updated;

    if (window == NULL) return;
    style = GetWindowLongPtrW(window, GWL_STYLE);
    updated = marquee != FALSE ?
        (style | (LONG_PTR)PBS_MARQUEE) :
        (style & ~(LONG_PTR)PBS_MARQUEE);
    if (updated == style) return;
    (void)SetWindowLongPtrW(window,
                            GWL_STYLE, updated);
    (void)SetWindowPos(window, NULL,
                       0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE |
                           SWP_NOZORDER | SWP_NOACTIVATE |
                           SWP_FRAMECHANGED);
}

static LRESULT WINAPI df_gui_send_message_w(
    HWND window, UINT message,
    WPARAM wparam, LPARAM lparam) {
    if (message == PBM_SETMARQUEE) {
        df_gui_set_progress_style(
            window, wparam != 0u ? TRUE : FALSE);
    }
    if (message == EM_REPLACESEL && lparam != 0) {
        static const wchar_t old_line[] = L"[ERROR] -\r\n";
        static const wchar_t new_line[] = L"[STATUS] OK\r\n";
        const wchar_t *text = (const wchar_t *)lparam;
        const wchar_t *match = wcsstr(text, old_line);

        if (match != NULL) {
            wchar_t patched[2048];
            const size_t prefix = (size_t)(match - text);
            const size_t old_length =
                (sizeof(old_line) / sizeof(old_line[0])) - 1u;
            const size_t new_length =
                (sizeof(new_line) / sizeof(new_line[0])) - 1u;
            const wchar_t *suffix = match + old_length;
            const size_t suffix_length = wcslen(suffix);
            const size_t required =
                prefix + new_length + suffix_length + 1u;

            if (required <=
                sizeof(patched) / sizeof(patched[0])) {
                if (prefix != 0u) {
                    (void)wmemcpy(patched,
                                  text, prefix);
                }
                (void)wmemcpy(patched + prefix,
                              new_line, new_length);
                (void)wmemcpy(
                    patched + prefix + new_length,
                    suffix, suffix_length + 1u);
                return df_gui_call_send_message_w(
                    window, message,
                    wparam, (LPARAM)patched);
            }
        }
    }
    return df_gui_call_send_message_w(
        window, message, wparam, lparam);
}
