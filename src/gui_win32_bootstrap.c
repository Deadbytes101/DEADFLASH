#if !defined(_WIN32)
#error "deadflash-gui is Windows-only"
#endif

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <stddef.h>
#include <wchar.h>

static BOOL df_gui_class_name_equals(
    LPCWSTR class_name, LPCWSTR expected) {
    if (class_name == NULL || expected == NULL ||
        IS_INTRESOURCE(class_name)) {
        return FALSE;
    }
    return lstrcmpiW(class_name, expected) == 0 ? TRUE : FALSE;
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
        df_gui_class_name_equals(class_name, PROGRESS_CLASSW);
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

    /*
     * Keep the proven light layout and hierarchy. Only selected operator
     * controls use classic Win32 rendering: square buttons, recessed fields,
     * classic combo boxes, and a segmented progress indicator.
     */
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

    /*
     * Built-in controls such as STATIC, EDIT, BUTTON, and COMBOBOX do not
     * require InitCommonControlsEx. Older or unusual comctl32 environments
     * may return FALSE with GetLastError()==ERROR_SUCCESS. The legacy call
     * still registers the progress class on those systems, so this condition
     * is not a valid reason to terminate before the main window exists.
     */
    InitCommonControls();
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static LRESULT WINAPI df_gui_call_send_message_w(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return SendMessageW(window, message, wparam, lparam);
}

static void df_gui_set_progress_style(HWND window, BOOL marquee) {
    LONG_PTR style;
    LONG_PTR updated;

    if (window == NULL) return;
    style = GetWindowLongPtrW(window, GWL_STYLE);
    updated = marquee != FALSE ?
        (style | (LONG_PTR)PBS_MARQUEE) :
        (style & ~(LONG_PTR)PBS_MARQUEE);
    if (updated == style) return;

    (void)SetWindowLongPtrW(window, GWL_STYLE, updated);
    (void)SetWindowPos(window, NULL, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static LRESULT WINAPI df_gui_send_message_w(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == PBM_SETMARQUEE) {
        df_gui_set_progress_style(window,
                                  wparam != 0u ? TRUE : FALSE);
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

            if (required <= sizeof(patched) / sizeof(patched[0])) {
                if (prefix != 0u) {
                    (void)wmemcpy(patched, text, prefix);
                }
                (void)wmemcpy(patched + prefix,
                              new_line, new_length);
                (void)wmemcpy(patched + prefix + new_length,
                              suffix, suffix_length + 1u);
                return df_gui_call_send_message_w(
                    window, message, wparam, (LPARAM)patched);
            }
        }
    }

    return df_gui_call_send_message_w(
        window, message, wparam, lparam);
}

#define CreateWindowExW df_gui_create_window_ex_w
#define InitCommonControlsEx df_gui_init_common_controls
#define SendMessageW df_gui_send_message_w
#include "gui_win32_final.c"
