#ifndef DEADFLASH_REPORT_H
#define DEADFLASH_REPORT_H

#include "deadflash/fat32.h"

typedef struct df_report_context {
    const char *operation;
    const char *source_path;
    const char *target_path;
    const char *plan_seal;
    const df_target_info *target;
    const df_write_options *write_options;
    const df_write_result *write_result;
    const df_fat32_layout *fat32_layout;
    df_status status;
    const df_error *error;
} df_report_context;

df_status df_write_json_report(const char *path, const df_report_context *context, df_error *error);

#endif
