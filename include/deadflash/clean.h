#ifndef DEADFLASH_CLEAN_H
#define DEADFLASH_CLEAN_H

#include "deadflash/io.h"

typedef enum df_clean_partition_style {
    DF_CLEAN_PARTITION_UNKNOWN = 0,
    DF_CLEAN_PARTITION_RAW,
    DF_CLEAN_PARTITION_MBR,
    DF_CLEAN_PARTITION_GPT
} df_clean_partition_style;

typedef struct df_clean_result {
    char final_state[48];
    unsigned disk_number;
    bool layout_deleted;
    df_clean_partition_style before_style;
    df_clean_partition_style after_style;
    unsigned before_partition_count;
    unsigned after_partition_count;
    double clean_ms;
    double verify_ms;
    double total_ms;
    df_target_info before_target;
    df_target_info after_target;
} df_clean_result;

const char *df_clean_partition_style_name(df_clean_partition_style style);

df_status df_clean_disk(const char *target_path,
                        const char *confirmation_token,
                        df_clean_result *result,
                        df_error *error);

df_status df_write_clean_json_report(const char *path,
                                     const char *target_path,
                                     const df_clean_result *result,
                                     df_status status,
                                     const df_error *operation_error,
                                     df_error *report_error);

#endif
