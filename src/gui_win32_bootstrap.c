#if !defined(_WIN32)
#error "deadflash-gui is Windows-only"
#endif

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#include <stddef.h>
#include <wchar.h>

#define DF_RETRO_STATUS_CONTROL_ID 1016

static const COLORREF DF_RETRO_BACKGROUND = RGB(0, 18, 42);
static const COLORREF DF_RETRO_PANEL = RGB(0, 0, 96);
static const COLORREF DF_RETRO_EDIT = RGB(0, 0, 64);
static const COLORREF DF_RETRO_BUTTON = RGB(192, 192, 192);
static const COLORREF DF_RETRO_AMBER = RGB(255, 224, 112);
static const COLORREF DF_RETRO_TEXT = RGB(232, 232, 208);
static const COLORREF DF_RETRO_SUCCESS = RGB(128, 255, 128);
static const COLORREF DF_RETRO_WARNING = RGB(255, 224, 112);
static const COLORREF DF_RETRO_ERROR = RGB(255, 128, 128);

static WNDPROC g_df_gui_original_window_proc = NULL;
static HBRUSH g_df_gui_background_brush = NULL;
static HBRUSH g_df_gui_panel_brush = NULL;
static HBRUSH g_df_gui_edit_brush = NULL;
static HBRUSH g_df_gui_button_brush = NULL;
static HFONT g_df_gui_retro_font = NULL;
static HFONT g_df_gui_retro_bold_font = NULL;

static void df_gui_retro_release_resources(void) {
    if (g_df_gui_retro_font != NULL) {
        (void)DeleteObject(g_df_gui_retro_font);
        g_df_gui_retro_font = NULL;
    }
    if (g_df_gui_retro_bold_font != NULL) {
        (void)DeleteObject(g_df_gui_retro_bold_font);
        g_df_gui_retro_bold_font = NULL;
    }
    if (g_df_gui_background_brush != NULL) {
        (void)DeleteObject(g_df_gui_background_brush);
        g_df_gui_background_brush = NULL;
    }
    if (g_df_gui_panel_brush != NULL) {
        (void)DeleteObject(g_df_gui_panel_brush);
        g_df_gui_panel_brush = NULL;
    }
    if (g_df_gui_edit_brush != NULL) {
        (void)DeleteObject(g_df_gui_edit_brush);
        g_df_gui_edit_brush = NULL;
    }
    if (g_df_gui_button_brush != NULL) {
        (void)DeleteObject(g_df_gui_button_brush);
        g_df_gui_button_brush = NULL;
    }
}

static void df_gui_retro_ensure_resources(void) {
    if (g_df_gui_background_brush == NULL) {
        g_df_gui_background_brush =
            CreateSolidBrush(DF_RETRO_BACKGROUND);
    }
    if (g_df_gui_panel_brush == NULL) {
        g_df_gui_panel_brush = CreateSolidBrush(DF_RETRO_PANEL);
    }
    if (g_df_gui_edit_brush == NULL) {
        g_df_gui_edit_brush = CreateSolidBrush(DF_RETRO_EDIT);
    }
    if (g_df_gui_button_brush == NULL) {
        g_df_gui_button_brush = CreateSolidBrush(DF_RETRO_BUTTON);
    }
    if (g_df_gui_retro_font == NULL) {
        g_df_gui_retro_font = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            FIXED_PITCH | FF_MODERN, L"Lucida Console");
    }
    if (g_df_gui_retro_bold_font == NULL) {
        g_df_gui_retro_bold_font = CreateFontW(
            -15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            FIXED_PITCH | FF_MODERN, L"Lucida Console");
    }
}

static COLORREF df_gui_retro_status_text_color(HWND control) {
    wchar_t text[160];
    text[0] = L'\0';
    (void)GetWindowTextW(control, text,
                         (int)(sizeof(text) / sizeof(text[0])));
    if (wcsstr(text, L"FAILED") != NULL ||
        wcsstr(text, L"ERROR") != NULL ||
        wcsstr(text, L"BLOCKED") != NULL) {
        return DF_RETRO_ERROR;
    }
    if (wcsstr(text, L"COMPLETE") != NULL ||
        wcsstr(text, L"READY") != NULL) {
        return DF_RETRO_SUCCESS;
    }
    return DF_RETRO_WARNING;
}

static LRESULT CALLBACK df_gui_retro_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            if (g_df_gui_background_brush != NULL) {
                RECT rect;
                if (GetClientRect(window, &rect)) {
                    (void)FillRect((HDC)wparam, &rect,
                                   g_df_gui_background_brush);
                    return 1;
                }
            }
            break;

        case WM_CTLCOLORSTATIC:
            {
                HDC dc = (HDC)wparam;
                HWND control = (HWND)lparam;
                const int id = GetDlgCtrlID(control);
                SetBkMode(dc, OPAQUE);
                if (id == DF_RETRO_STATUS_CONTROL_ID) {
                    SetTextColor(
                        dc, df_gui_retro_status_text_color(control));
                    SetBkColor(dc, DF_RETRO_PANEL);
                    return (LRESULT)g_df_gui_panel_brush;
                }
                SetTextColor(dc, DF_RETRO_AMBER);
                SetBkColor(dc, DF_RETRO_BACKGROUND);
                return (LRESULT)g_df_gui_background_brush;
            }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            {
                HDC dc = (HDC)wparam;
                SetTextColor(dc, DF_RETRO_TEXT);
                SetBkColor(dc, DF_RETRO_EDIT);
                return (LRESULT)g_df_gui_edit_brush;
            }

        case WM_CTLCOLORBTN:
            {
                HDC dc = (HDC)wparam;
                SetTextColor(dc, RGB(0, 0, 0));
                SetBkColor(dc, DF_RETRO_BUTTON);
                return (LRESULT)g_df_gui_button_brush;
            }

        default:
            break;
    }

    if (g_df_gui_original_window_proc != NULL) {
        const LRESULT result = CallWindowProcW(
            g_df_gui_original_window_proc,
            window, message, wparam, lparam);
        if (message == WM_DESTROY) {
            df_gui_retro_release_resources();
        }
        return result;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static ATOM WINAPI df_gui_register_class_ex_w(
    const WNDCLASSEXW *window_class) {
    WNDCLASSEXW retro_class;
    if (window_class == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0u;
    }

    df_gui_retro_ensure_resources();
    retro_class = *window_class;
    g_df_gui_original_window_proc = retro_class.lpfnWndProc;
    retro_class.lpfnWndProc = df_gui_retro_window_proc;
    if (g_df_gui_background_brush != NULL) {
        retro_class.hbrBackground = g_df_gui_background_brush;
    }
    return RegisterClassExW(&retro_class);
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
    LPCWSTR adjusted_title = window_name;
    HWND window;
    BOOL is_progress = FALSE;

    if (class_name != NULL && !IS_INTRESOURCE(class_name) &&
        lstrcmpiW(class_name, PROGRESS_CLASSW) == 0) {
        adjusted_style &= ~(DWORD)PBS_SMOOTH;
        is_progress = TRUE;
    }
    if ((style & WS_CHILD) == 0u && parent == NULL) {
        adjusted_title =
            L"DEADFLASH 1.0.0 // IMAGE WRITER + VERIFIER";
    }

    window = CreateWindowExW(
        extended_style, class_name, adjusted_title,
        adjusted_style, x, y, width, height,
        parent, menu, instance, parameter);
    if (window == NULL) return NULL;

    if ((style & WS_CHILD) != 0u) {
        (void)SetWindowTheme(window, L"", L"");
    }
    if (is_progress != FALSE) {
        (void)SendMessageW(window, PBM_SETBKCOLOR, 0u,
                           (LPARAM)DF_RETRO_EDIT);
        (void)SendMessageW(window, PBM_SETBARCOLOR, 0u,
                           (LPARAM)DF_RETRO_AMBER);
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
    if (message == WM_SETFONT) {
        LOGFONTW logical_font;
        HFONT replacement;
        df_gui_retro_ensure_resources();
        ZeroMemory(&logical_font, sizeof(logical_font));
        replacement = g_df_gui_retro_font;
        if (wparam != 0u &&
            GetObjectW((HFONT)wparam,
                       (int)sizeof(logical_font),
                       &logical_font) != 0 &&
            logical_font.lfWeight >= FW_BOLD) {
            replacement = g_df_gui_retro_bold_font;
        }
        if (replacement != NULL) {
            return df_gui_call_send_message_w(
                window, message, (WPARAM)replacement, lparam);
        }
    }

    if (message == PBM_SETMARQUEE) {
        df_gui_set_progress_style(window,
                                  wparam != 0u ? TRUE : FALSE);
    }

    if (message == PBM_SETSTATE) {
        COLORREF bar_color = DF_RETRO_AMBER;
        if ((int)wparam == PBST_ERROR) {
            bar_color = DF_RETRO_ERROR;
        } else if ((int)wparam == PBST_PAUSED) {
            bar_color = DF_RETRO_WARNING;
        }
        (void)df_gui_call_send_message_w(
            window, PBM_SETBKCOLOR, 0u,
            (LPARAM)DF_RETRO_EDIT);
        (void)df_gui_call_send_message_w(
            window, PBM_SETBARCOLOR, 0u,
            (LPARAM)bar_color);
        return 0;
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

#define RegisterClassExW df_gui_register_class_ex_w
#define CreateWindowExW df_gui_create_window_ex_w
#define InitCommonControlsEx df_gui_init_common_controls
#define SendMessageW df_gui_send_message_w
#include "gui_win32_final.c"
