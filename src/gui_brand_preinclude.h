#ifndef DEADFLASH_GUI_BRAND_PREINCLUDE_H
#define DEADFLASH_GUI_BRAND_PREINCLUDE_H

#if defined(_WIN32)
#include <windows.h>
#include "resource.h"

HICON WINAPI df_gui_load_icon_w(HINSTANCE instance, LPCWSTR name);
BOOL WINAPI df_gui_set_window_text_w(HWND window, LPCWSTR text);

#define LoadIconW df_gui_load_icon_w
#define SetWindowTextW df_gui_set_window_text_w
#endif

#endif
