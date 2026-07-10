#include "deadflash/fat32.h"
#include "deadflash/report.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out) {
    fprintf(out,
        "DEADFLASH %s\n"
        "WRITE THE IMAGE. VERIFY THE TRUTH.\n\n"
        "USAGE\n"
        "  deadflash version\n"
        "  deadflash list\n"
        "  deadflash inspect TARGET\n"
        "  deadflash plan IMAGE TARGET [--buffer SIZE]\n"
        "  deadflash write IMAGE TARGET [OPTIONS]\n"
        "  deadflash verify IMAGE TARGET [OPTIONS]\n"
        "  deadflash format-fat32 TARGET [OPTIONS]\n"
        "  deadflash verify-fat32 TARGET\n"
        "  deadflash bench TARGET [OPTIONS]\n\n"
        "WRITE OPTIONS\n"
        "  --verify none|sample|full   Default: full\n"
        "  --samples N                 Default: 64\n"
        "  --buffer SIZE               Default: 32MiB\n"
        "  --report PATH               Write JSON evidence\n"
        "  --direct                    Direct I/O for physical devices\n"
        "  --allow-device              Permit physical-device writes\n"
        "  --confirm TOKEN             Exact token printed by inspect\n"
        "  --force-mounted             Dangerous: allow mounted target\n"
        "  --force-system-disk         Extremely dangerous override\n\n"
        "FAT32 OPTIONS\n"
        "  --label TEXT                Default: DEADFLASH\n"
        "  --size SIZE                 Required for a new image file\n"
        "  --report PATH               Write JSON evidence\n"
        "  --allow-device --confirm TOKEN\n\n",
        DF_VERSION_STRING);
}

static int fail(const df_error *error) {
    if (error != NULL) {
        fprintf(stderr, "ERROR[%s]", df_status_name(error->code));
        if (error->os_code != 0) fprintf(stderr, "(os=%d)", error->os_code);
        if (error->message[0] != '\0') fprintf(stderr, ": %s", error->message);
        fputc('\n', stderr);
    }
    return 1;
}

static const char *next_value(int argc, char **argv, int *index, const char *name) {
    const char *arg = argv[*index];
    size_t n = strlen(name);
    if (strncmp(arg, name, n) == 0 && arg[n] == '=') return arg + n + 1;
    if (strcmp(arg, name) == 0 && *index + 1 < argc) return argv[++(*index)];
    return NULL;
}

static df_verify_mode parse_verify_mode(const char *text, bool *ok) {
    *ok = true;
    if (strcmp(text, "none") == 0) return DF_VERIFY_NONE;
    if (strcmp(text, "sample") == 0) return DF_VERIFY_SAMPLE;
    if (strcmp(text, "full") == 0) return DF_VERIFY_FULL;
    *ok = false;
    return DF_VERIFY_NONE;
}

static void print_target(const df_target_info *info) {
    printf("PATH               : %s\n", info->path);
    printf("KIND               : %u\n", (unsigned)info->kind);
    printf("SIZE_BYTES         : %" PRIu64 "\n", info->size_bytes);
    printf("LOGICAL_SECTOR     : %u\n", info->logical_sector_size);
    printf("PHYSICAL_SECTOR    : %u\n", info->physical_sector_size);
    printf("REMOVABLE          : %s\n", info->removable ? "YES" : "NO");
    printf("READ_ONLY          : %s\n", info->read_only ? "YES" : "NO");
    printf("MOUNTED            : %s\n", info->mounted ? "YES" : "NO");
    printf("SYSTEM_DISK        : %s\n", info->system_disk ? "YES" : "NO");
    printf("CONFIRMATION_TOKEN : %s\n", info->token);
}

static int command_inspect(const char *path) {
    df_target_info info;
    df_error error;
    if (df_inspect_target(path, &info, &error) != DF_OK) return fail(&error);
    print_target(&info);
    return 0;
}

static int command_plan(int argc, char **argv) {
    df_file source;
    df_target_info target;
    df_error error;
    df_status status;
    uint8_t digest[32];
    char hex[65];
    size_t buffer_size = DF_DEFAULT_BUFFER_SIZE;
    int i;
    if (argc < 4) { usage(stderr); return 2; }
    for (i = 4; i < argc; ++i) {
        const char *value = next_value(argc, argv, &i, "--buffer");
        uint64_t parsed;
        if (value == NULL || !df_parse_size(value, &parsed) || parsed == 0 || parsed > SIZE_MAX) return 2;
        buffer_size = (size_t)parsed;
    }
    memset(&source, 0, sizeof(source));
    source.handle = DF_INVALID_HANDLE;
    status = df_open_source(argv[2], &source, &error);
    if (status != DF_OK) return fail(&error);
    status = df_inspect_target(argv[3], &target, &error);
    if (status != DF_OK) {
        df_close_file(&source);
        return fail(&error);
    }
    status = df_hash_file_region(&source, source.size_bytes, buffer_size, digest, &error);
    if (status != DF_OK) {
        df_close_file(&source);
        return fail(&error);
    }
    df_close_file(&source);
    df_hex_encode(digest, 32, hex);
    printf("OPERATION          : WRITE_IMAGE\n");
    printf("SOURCE             : %s\n", argv[2]);
    printf("SOURCE_SIZE_BYTES  : %" PRIu64 "\n", source.size_bytes);
    printf("SOURCE_SHA256      : %s\n", hex);
    printf("TARGET             : %s\n", argv[3]);
    printf("TARGET_SIZE_BYTES  : %" PRIu64 "\n", target.size_bytes);
    printf("TARGET_KIND        : %u\n", (unsigned)target.kind);
    printf("TARGET_MOUNTED     : %s\n", target.mounted ? "YES" : "NO");
    printf("TARGET_SYSTEM_DISK : %s\n", target.system_disk ? "YES" : "NO");
    printf("CONFIRMATION_TOKEN : %s\n", target.token);
    if (target.kind == DF_TARGET_REGULAR_FILE) {
        printf("EXECUTE            : deadflash write \"%s\" \"%s\" --verify full --report run.json\n",
               argv[2], argv[3]);
    } else {
        printf("EXECUTE            : deadflash write \"%s\" \"%s\" --allow-device --confirm %s --verify full --report run.json\n",
               argv[2], argv[3], target.token);
    }
    return 0;
}

static int command_write(int argc, char **argv) {
    const char *source;
    const char *target;
    const char *report_path = NULL;
    df_write_options options;
    df_write_result result;
    df_target_info info;
    df_error error;
    df_status status;
    int i;
    if (argc < 4) { usage(stderr); return 2; }
    source = argv[2];
    target = argv[3];
    memset(&options, 0, sizeof(options));
    options.verify_mode = DF_VERIFY_FULL;
    options.buffer_size = DF_DEFAULT_BUFFER_SIZE;
    options.write_retries = 4;
    options.sample_count = 64;
    options.truncate_regular_file = true;
    memset(&result, 0, sizeof(result));
    memset(&info, 0, sizeof(info));
    df_error_clear(&error);

    for (i = 4; i < argc; ++i) {
        const char *value;
        if ((value = next_value(argc, argv, &i, "--verify")) != NULL) {
            bool ok;
            options.verify_mode = parse_verify_mode(value, &ok);
            if (!ok) { fprintf(stderr, "invalid verify mode: %s\n", value); return 2; }
        } else if ((value = next_value(argc, argv, &i, "--samples")) != NULL) {
            uint64_t parsed;
            if (!df_parse_u64(value, &parsed) || parsed > UINT32_MAX) return 2;
            options.sample_count = (unsigned)parsed;
        } else if ((value = next_value(argc, argv, &i, "--buffer")) != NULL) {
            uint64_t parsed;
            if (!df_parse_size(value, &parsed) || parsed == 0 || parsed > SIZE_MAX) return 2;
            options.buffer_size = (size_t)parsed;
        } else if ((value = next_value(argc, argv, &i, "--report")) != NULL) {
            report_path = value;
        } else if ((value = next_value(argc, argv, &i, "--confirm")) != NULL) {
            options.confirmation_token = value;
        } else if (strcmp(argv[i], "--allow-device") == 0) options.allow_device = true;
        else if (strcmp(argv[i], "--force-mounted") == 0) options.force_mounted = true;
        else if (strcmp(argv[i], "--force-system-disk") == 0) options.force_system_disk = true;
        else if (strcmp(argv[i], "--direct") == 0) options.direct_io = true;
        else { fprintf(stderr, "unknown option: %s\n", argv[i]); return 2; }
    }

    status = df_write_image(source, target, &options, &info, &result, &error);
    printf("STATE          : %s\n", result.final_state);
    printf("BYTES_WRITTEN  : %" PRIu64 "\n", result.bytes_written);
    printf("WRITE_MS       : %.3f\n", result.write_ms);
    printf("FLUSH_MS       : %.3f\n", result.flush_ms);
    printf("VERIFY_MS      : %.3f\n", result.verify_ms);
    printf("TOTAL_MS       : %.3f\n", result.total_ms);
    if (result.write_ms > 0.0)
        printf("WRITE_MIB_S    : %.3f\n", ((double)result.bytes_written / 1048576.0) / (result.write_ms / 1000.0));
    if (report_path != NULL) {
        df_report_context context;
        df_error report_error;
        memset(&context, 0, sizeof(context));
        context.operation = "write";
        context.source_path = source;
        context.target_path = target;
        context.target = &info;
        context.write_options = &options;
        context.write_result = &result;
        context.status = status;
        context.error = &error;
        if (df_write_json_report(report_path, &context, &report_error) != DF_OK)
            fprintf(stderr, "REPORT ERROR: %s\n", report_error.message);
    }
    return status == DF_OK ? 0 : fail(&error);
}

static int command_verify(int argc, char **argv) {
    df_verify_mode mode = DF_VERIFY_FULL;
    unsigned samples = 64;
    size_t buffer_size = DF_DEFAULT_BUFFER_SIZE;
    df_write_result result;
    df_error error;
    df_status status;
    int i;
    if (argc < 4) { usage(stderr); return 2; }
    for (i = 4; i < argc; ++i) {
        const char *value;
        if ((value = next_value(argc, argv, &i, "--verify")) != NULL) {
            bool ok;
            mode = parse_verify_mode(value, &ok);
            if (!ok) return 2;
        } else if ((value = next_value(argc, argv, &i, "--samples")) != NULL) {
            uint64_t parsed;
            if (!df_parse_u64(value, &parsed) || parsed > UINT32_MAX) return 2;
            samples = (unsigned)parsed;
        } else if ((value = next_value(argc, argv, &i, "--buffer")) != NULL) {
            uint64_t parsed;
            if (!df_parse_size(value, &parsed) || parsed == 0 || parsed > SIZE_MAX) return 2;
            buffer_size = (size_t)parsed;
        } else return 2;
    }
    status = df_verify_image(argv[2], argv[3], mode, samples, buffer_size, &result, &error);
    printf("STATE          : %s\n", result.final_state);
    printf("BYTES_VERIFIED : %" PRIu64 "\n", result.bytes_verified);
    printf("VERIFY_MS      : %.3f\n", result.verify_ms);
    return status == DF_OK ? 0 : fail(&error);
}

static int command_format_fat32(int argc, char **argv) {
    df_fat32_options options;
    df_fat32_layout layout;
    df_target_info info;
    df_error error;
    df_status status;
    const char *report_path = NULL;
    int i;
    if (argc < 3) { usage(stderr); return 2; }
    memset(&options, 0, sizeof(options));
    memset(&layout, 0, sizeof(layout));
    memset(&info, 0, sizeof(info));
    df_error_clear(&error);
    options.create_mbr = true;
    options.partition_start_lba = 2048;
    memcpy(options.label, "DEADFLASH", 10);
    for (i = 3; i < argc; ++i) {
        const char *value;
        if ((value = next_value(argc, argv, &i, "--label")) != NULL) {
            df_fat32_normalize_label(value, options.label);
        } else if ((value = next_value(argc, argv, &i, "--size")) != NULL) {
            if (!df_parse_size(value, &options.regular_file_size)) return 2;
        } else if ((value = next_value(argc, argv, &i, "--confirm")) != NULL) {
            options.confirmation_token = value;
        } else if ((value = next_value(argc, argv, &i, "--report")) != NULL) {
            report_path = value;
        } else if (strcmp(argv[i], "--allow-device") == 0) options.allow_device = true;
        else if (strcmp(argv[i], "--force-mounted") == 0) options.force_mounted = true;
        else if (strcmp(argv[i], "--force-system-disk") == 0) options.force_system_disk = true;
        else return 2;
    }
    status = df_format_fat32(argv[2], &options, &info, &layout, &error);
    if (status == DF_OK) {
        printf("STATE               : SUCCESS_VERIFIED_STRUCTURE\n");
        printf("PARTITION_START_LBA : %u\n", layout.partition_start_lba);
        printf("PARTITION_SECTORS   : %u\n", layout.partition_sectors);
        printf("SECTORS_PER_CLUSTER : %u\n", layout.sectors_per_cluster);
        printf("SECTORS_PER_FAT     : %u\n", layout.sectors_per_fat);
    }
    if (report_path != NULL) {
        df_report_context context;
        df_error report_error;
        memset(&context, 0, sizeof(context));
        context.operation = "format-fat32";
        context.target_path = argv[2];
        context.target = &info;
        context.fat32_layout = &layout;
        context.status = status;
        context.error = &error;
        if (df_write_json_report(report_path, &context, &report_error) != DF_OK)
            fprintf(stderr, "REPORT ERROR: %s\n", report_error.message);
    }
    return status == DF_OK ? 0 : fail(&error);
}

static int command_bench(int argc, char **argv) {
    const char *target;
    uint64_t size = 256ULL * 1024ULL * 1024ULL;
    unsigned runs = 5;
    char source_path[4096];
    FILE *source;
    uint8_t *buffer;
    size_t chunk = 1024u * 1024u;
    uint64_t written = 0;
    unsigned i, run;
    double *samples;
    df_error error;
    if (argc < 3) { usage(stderr); return 2; }
    target = argv[2];
    for (i = 3; i < (unsigned)argc; ++i) {
        const char *value;
        int index = (int)i;
        if ((value = next_value(argc, argv, &index, "--size")) != NULL) {
            if (!df_parse_size(value, &size)) return 2;
            i = (unsigned)index;
        } else if ((value = next_value(argc, argv, &index, "--runs")) != NULL) {
            uint64_t parsed;
            if (!df_parse_u64(value, &parsed) || parsed == 0 || parsed > 100) return 2;
            runs = (unsigned)parsed;
            i = (unsigned)index;
        } else return 2;
    }
    snprintf(source_path, sizeof(source_path), "%s.deadflash-bench-source", target);
    source = fopen(source_path, "wb");
    if (source == NULL) { fprintf(stderr, "cannot create benchmark source\n"); return 1; }
    buffer = (uint8_t *)malloc(chunk);
    if (buffer == NULL) { fclose(source); return 1; }
    for (i = 0; i < chunk; ++i) buffer[i] = (uint8_t)((i * 131u + 17u) & 0xffu);
    while (written < size) {
        size_t n = (size - written) < chunk ? (size_t)(size - written) : chunk;
        if (fwrite(buffer, 1, n, source) != n) { free(buffer); fclose(source); return 1; }
        written += n;
    }
    free(buffer);
    if (fclose(source) != 0) return 1;
    samples = (double *)calloc(runs, sizeof(double));
    if (samples == NULL) { remove(source_path); return 1; }
    for (run = 0; run < runs; ++run) {
        df_write_options options;
        df_write_result result;
        df_target_info info;
        memset(&options, 0, sizeof(options));
        options.verify_mode = DF_VERIFY_FULL;
        options.buffer_size = DF_DEFAULT_BUFFER_SIZE;
        options.write_retries = 4;
        options.truncate_regular_file = true;
        if (df_write_image(source_path, target, &options, &info, &result, &error) != DF_OK) {
            free(samples); remove(source_path); return fail(&error);
        }
        samples[run] = result.total_ms;
        printf("RUN %u: write=%.3f MiB/s total=%.3f ms verified=%" PRIu64 "\n",
               run + 1u,
               result.write_ms > 0.0 ? ((double)result.bytes_written / 1048576.0) / (result.write_ms / 1000.0) : 0.0,
               result.total_ms, result.bytes_verified);
    }
    for (i = 0; i < runs; ++i) {
        unsigned j;
        for (j = i + 1; j < runs; ++j) {
            if (samples[j] < samples[i]) { double t = samples[i]; samples[i] = samples[j]; samples[j] = t; }
        }
    }
    printf("MEDIAN_TOTAL_MS: %.3f\n", samples[runs / 2u]);
    free(samples);
    remove(source_path);
    return 0;
}

int main(int argc, char **argv) {
    df_error error;
    if (argc < 2) { usage(stdout); return 0; }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("DEADFLASH %s\n", DF_VERSION_STRING);
        return 0;
    }
    if (strcmp(argv[1], "list") == 0) {
        return df_list_targets(stdout, &error) == DF_OK ? 0 : fail(&error);
    }
    if (strcmp(argv[1], "inspect") == 0 && argc == 3) return command_inspect(argv[2]);
    if (strcmp(argv[1], "plan") == 0) return command_plan(argc, argv);
    if (strcmp(argv[1], "write") == 0) return command_write(argc, argv);
    if (strcmp(argv[1], "verify") == 0) return command_verify(argc, argv);
    if (strcmp(argv[1], "format-fat32") == 0) return command_format_fat32(argc, argv);
    if (strcmp(argv[1], "verify-fat32") == 0 && argc == 3) {
        df_fat32_layout layout;
        char label[12];
        if (df_verify_fat32(argv[2], &layout, label, &error) != DF_OK) return fail(&error);
        printf("STATE : VALID_FAT32\nLABEL : %.11s\n", label);
        return 0;
    }
    if (strcmp(argv[1], "bench") == 0) return command_bench(argc, argv);
    usage(stderr);
    return 2;
}
