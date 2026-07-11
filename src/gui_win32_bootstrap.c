#if !defined(_WIN32)
#error "deadflash-gui is Windows-only"
#endif

#include <windows.h>
#include <commctrl.h>

static BOOL df_gui_init_common_controls(const INITCOMMONCONTROLSEX *requested) {
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

#define InitCommonControlsEx df_gui_init_common_controls
#include "gui_win32_final.c"
