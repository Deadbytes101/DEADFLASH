#include "deadflash/clean.h"

#include <string.h>

const char *df_clean_partition_style_name(df_clean_partition_style style) {
    switch (style) {
        case DF_CLEAN_PARTITION_RAW: return "raw";
        case DF_CLEAN_PARTITION_MBR: return "mbr";
        case DF_CLEAN_PARTITION_GPT: return "gpt";
        case DF_CLEAN_PARTITION_UNKNOWN:
        default: return "unknown";
    }
}

df_status df_clean_disk(const char *target_path,
                        const char *confirmation_token,
                        df_clean_result *result,
                        df_error *error) {
    (void)target_path;
    (void)confirmation_token;
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
        (void)snprintf(result->final_state,
                       sizeof(result->final_state),
                       "%s", "unsupported");
    }
    df_error_set(error, DF_ERR_UNSUPPORTED, 0,
                 "whole-disk clean is currently implemented on Windows only");
    return DF_ERR_UNSUPPORTED;
}

df_status df_write_clean_json_report(const char *path,
                                     const char *target_path,
                                     const df_clean_result *result,
                                     df_status status,
                                     const df_error *operation_error,
                                     df_error *report_error) {
    (void)path;
    (void)target_path;
    (void)result;
    (void)status;
    (void)operation_error;
    df_error_set(report_error, DF_ERR_UNSUPPORTED, 0,
                 "clean evidence is currently implemented on Windows only");
    return DF_ERR_UNSUPPORTED;
}
