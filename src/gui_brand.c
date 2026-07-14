#include "gui_brand_preinclude.h"

#undef LoadIconW

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
