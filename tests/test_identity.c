#include "deadflash/io.h"

#include <stdio.h>
#include <string.h>

#define TARGET_PATH "deadflash-test-identity.bin"

static int write_size(size_t size) {
    FILE *file = fopen(TARGET_PATH, "wb");
    uint8_t block[4096];
    size_t remaining = size;
    memset(block, 0x5a, sizeof(block));
    if (file == NULL) return 1;
    while (remaining > 0) {
        size_t count = remaining < sizeof(block) ? remaining : sizeof(block);
        if (fwrite(block, 1, count, file) != count) {
            fclose(file);
            return 1;
        }
        remaining -= count;
    }
    return fclose(file) != 0;
}

int main(void) {
    df_target_info planned;
    df_write_options options;
    df_file target;
    df_error error;
    df_status status;
    int rc = 1;

    remove(TARGET_PATH);
    if (write_size(4096) != 0) {
        fprintf(stderr, "could not create identity target\n");
        goto out;
    }
    status = df_inspect_target(TARGET_PATH, &planned, &error);
    if (status != DF_OK) {
        fprintf(stderr, "initial inspection failed: %s\n", error.message);
        goto out;
    }

    if (write_size(8192) != 0) {
        fprintf(stderr, "could not mutate identity target\n");
        goto out;
    }

    memset(&options, 0, sizeof(options));
    memset(&target, 0, sizeof(target));
    target.handle = DF_INVALID_HANDLE;
    status = df_open_target(TARGET_PATH, &planned, &options, &target, &error);
    if (status != DF_ERR_IDENTITY_CHANGED) {
        fprintf(stderr, "stale target plan was accepted: status=%s error=%s\n",
                df_status_name(status), error.message);
        df_close_file(&target);
        goto out;
    }
    rc = 0;
out:
    remove(TARGET_PATH);
    return rc;
}
