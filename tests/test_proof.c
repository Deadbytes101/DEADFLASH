#include "deadflash/proof.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int make_file(const char *path, size_t length) {
    FILE *file = fopen(path, "wb");
    size_t i;
    if (file == NULL) return 0;
    for (i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)((i * 131u + 17u) & 255u);
        if (fwrite(&byte, 1u, 1u, file) != 1u) {
            fclose(file);
            return 0;
        }
    }
    return fclose(file) == 0;
}

static int copy_file(const char *source, const char *target) {
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    unsigned char buffer[8192];
    size_t length;
    if (input == NULL || output == NULL) {
        if (input != NULL) fclose(input);
        if (output != NULL) fclose(output);
        return 0;
    }
    while ((length = fread(buffer, 1u, sizeof(buffer), input)) > 0u) {
        if (fwrite(buffer, 1u, length, output) != length) {
            fclose(input);
            fclose(output);
            return 0;
        }
    }
    fclose(input);
    return fclose(output) == 0;
}

static int corrupt_byte(const char *path, uint64_t offset) {
    FILE *file = fopen(path, "r+b");
    int value;
    if (file == NULL || fseek(file, (long)offset, SEEK_SET) != 0) return 0;
    value = fgetc(file);
    if (value == EOF || fseek(file, (long)offset, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    value ^= 0xff;
    if (fputc(value, file) == EOF) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

int main(void) {
    const char *source = "proof-source.bin";
    const char *target = "proof-target.bin";
    const char *manifest = "proof.dfp";
    const uint64_t bad_offset = 2u * 1024u * 1024u + 17u;
    df_error error;
    df_proof_summary summary;
    df_proof_result result;

    if (!make_file(source, 3u * 1024u * 1024u + 123u) ||
        !copy_file(source, target))
        return 1;
    if (df_proof_create(source, manifest, 1024u * 1024u,
                        &summary, &error) != DF_OK)
        return 2;
    if (summary.chunk_count != 4u) return 3;
    if (df_proof_verify(manifest, source, target,
                        &result, &error) != DF_OK ||
        strcmp(result.final_state, "success_proven") != 0)
        return 4;

    if (!corrupt_byte(target, bad_offset)) return 5;
    if (df_proof_verify(manifest, source, target,
                        &result, &error) != DF_ERR_VERIFY_MISMATCH)
        return 6;
    if (result.first_bad_offset != bad_offset) return 7;

    if (!copy_file(source, target) || !corrupt_byte(source, 9u)) return 8;
    if (df_proof_verify(manifest, source, target,
                        &result, &error) != DF_ERR_IDENTITY_CHANGED)
        return 9;

    (void)remove(source);
    (void)remove(target);
    (void)remove(manifest);
    return 0;
}
