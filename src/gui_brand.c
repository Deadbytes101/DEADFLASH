#include "gui_brand_preinclude.h"

#undef LoadIconW
#undef SetWindowTextW

HICON WINAPI df_gui_load_icon_w(HINSTANCE instance, LPCWSTR name) {
    if (instance == NULL && name == IDI_APPLICATION) {
        HINSTANCE module = GetModuleHandleW(NULL);
        HICON branded = (HICON)LoadImageW(
            module,
            MAKEINTRESOURCEW(IDI_DEADFLASH),
            IMAGE_ICON,
            0,
            0,
            LR_DEFAULTSIZE | LR_SHARED);
        if (branded != NULL) return branded;
    }
    return LoadIconW(instance, name);
}

BOOL WINAPI df_gui_set_window_text_w(HWND window, LPCWSTR text) {
    static const wchar_t original_header[] =
        L"DEADFLASH 1.0.0   |   WRITE THE IMAGE. VERIFY THE TRUTH.";
    static const wchar_t branded_header[] =
        L"DEADBYTE // DEADFLASH 1.0.0   |   WRITE THE IMAGE. VERIFY THE TRUTH.";

    if (text != NULL && lstrcmpW(text, original_header) == 0) {
        text = branded_header;
    }
    return SetWindowTextW(window, text);
}
