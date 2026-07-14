#include "deadflash/clean.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect_true(int condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static void test_style_names(void) {
    expect_true(strcmp(df_clean_partition_style_name(
                           DF_CLEAN_PARTITION_RAW),
                       "raw") == 0,
                "raw style name");
    expect_true(strcmp(df_clean_partition_style_name(
                           DF_CLEAN_PARTITION_MBR),
                       "mbr") == 0,
                "mbr style name");
    expect_true(strcmp(df_clean_partition_style_name(
                           DF_CLEAN_PARTITION_GPT),
                       "gpt") == 0,
                "gpt style name");
}

static void test_invalid_arguments(void) {
    df_clean_result result;
    df_error error;
    df_status status = df_clean_disk(NULL, NULL, &result, &error);
    expect_true(status == DF_ERR_INVALID_ARGUMENT,
                "null target rejected");
    expect_true(strcmp(result.final_state,
                       "failed_before_clean") == 0,
                "null target fails before clean");
}

static void test_regular_file_rejected(void) {
    static const char path[] = "deadflash-clean-test-target.bin";
    static const unsigned char payload[] = {
        0x44u, 0x45u, 0x41u, 0x44u,
        0x46u, 0x4cu, 0x41u, 0x53u,
        0x48u
    };
    unsigned char observed[sizeof(payload)];
    df_clean_result result;
    df_error error;
    df_status status;
    FILE *file = fopen(path, "wb");

    expect_true(file != NULL, "create regular clean target");
    if (file == NULL) return;
    expect_true(fwrite(payload, 1u, sizeof(payload), file) ==
                    sizeof(payload),
                "write regular clean target");
    expect_true(fclose(file) == 0, "close regular clean target");

    status = df_clean_disk(path, "not-a-device-token",
                           &result, &error);
#if defined(_WIN32)
    expect_true(status == DF_ERR_DEVICE_REQUIRED,
                "regular file rejected as clean target");
#else
    expect_true(status == DF_ERR_UNSUPPORTED,
                "clean reports unsupported off Windows");
#endif
    expect_true(!result.layout_deleted,
                "regular file layout was not deleted");

    file = fopen(path, "rb");
    expect_true(file != NULL, "reopen regular clean target");
    if (file != NULL) {
        memset(observed, 0, sizeof(observed));
        expect_true(fread(observed, 1u, sizeof(observed), file) ==
                        sizeof(observed),
                    "read regular clean target");
        expect_true(memcmp(observed, payload,
                           sizeof(payload)) == 0,
                    "regular file remains unchanged");
        (void)fclose(file);
    }
    (void)remove(path);
}

int main(void) {
    test_style_names();
    test_invalid_arguments();
    test_regular_file_rejected();
    if (failures != 0) {
        fprintf(stderr, "%d clean test(s) failed\n", failures);
        return 1;
    }
    puts("clean tests passed");
    return 0;
}
