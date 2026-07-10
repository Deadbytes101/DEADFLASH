#include "deadflash/report.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static void json_string(FILE *file, const char *text) {
    const unsigned char *p = (const unsigned char *)(text != NULL ? text : "");
    fputc('"', file);
    while (*p != '\0') {
        switch (*p) {
            case '"': fputs("\\\"", file); break;
            case '\\': fputs("\\\\", file); break;
            case '\b': fputs("\\b", file); break;
            case '\f': fputs("\\f", file); break;
            case '\n': fputs("\\n", file); break;
            case '\r': fputs("\\r", file); break;
            case '\t': fputs("\\t", file); break;
            default:
                if (*p < 0x20) fprintf(file, "\\u%04x", (unsigned)*p);
                else fputc((int)*p, file);
                break;
        }
        ++p;
    }
    fputc('"', file);
}

static const char *verify_mode_name(df_verify_mode mode) {
    switch (mode) {
        case DF_VERIFY_NONE: return "none";
        case DF_VERIFY_SAMPLE: return "sample";
        case DF_VERIFY_FULL: return "full";
        default: return "unknown";
    }
}

df_status df_write_json_report(const char *path, const df_report_context *context, df_error *error) {
    FILE *file;
    char source_hash[65] = {0};
    char target_hash[65] = {0};
    if (path == NULL || context == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "report path and context are required");
        return DF_ERR_INVALID_ARGUMENT;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        df_error_set(error, DF_ERR_OPEN, 0, "could not create report '%s'", path);
        return DF_ERR_OPEN;
    }
    if (context->write_result != NULL) {
        df_hex_encode(context->write_result->source_sha256, 32, source_hash);
        df_hex_encode(context->write_result->target_sha256, 32, target_hash);
    }
    fputs("{\n  \"schema\": \"deadflash.evidence.v1\",\n", file);
    fputs("  \"tool\": {\"name\": \"DEADFLASH\", \"version\": \"" DF_VERSION_STRING "\"},\n", file);
    fprintf(file, "  \"timestamp_unix_ms\": %" PRIu64 ",\n", df_unix_time_ms());
    fputs("  \"operation\": ", file); json_string(file, context->operation); fputs(",\n", file);
    fputs("  \"status\": ", file); json_string(file, df_status_name(context->status)); fputs(",\n", file);
    fputs("  \"source\": ", file); json_string(file, context->source_path); fputs(",\n", file);
    fputs("  \"target_path\": ", file); json_string(file, context->target_path); fputs(",\n", file);

    if (context->target != NULL) {
        fputs("  \"target\": {\n", file);
        fprintf(file, "    \"kind\": %u,\n", (unsigned)context->target->kind);
        fprintf(file, "    \"size_bytes\": %" PRIu64 ",\n", context->target->size_bytes);
        fprintf(file, "    \"logical_sector_size\": %u,\n", context->target->logical_sector_size);
        fprintf(file, "    \"physical_sector_size\": %u,\n", context->target->physical_sector_size);
        fprintf(file, "    \"removable\": %s,\n", context->target->removable ? "true" : "false");
        fprintf(file, "    \"mounted\": %s,\n", context->target->mounted ? "true" : "false");
        fprintf(file, "    \"system_disk\": %s,\n", context->target->system_disk ? "true" : "false");
        fputs("    \"token\": ", file); json_string(file, context->target->token); fputs("\n  },\n", file);
    } else {
        fputs("  \"target\": null,\n", file);
    }

    if (context->write_options != NULL) {
        fputs("  \"configuration\": {\n", file);
        fprintf(file, "    \"buffer_bytes\": %zu,\n", context->write_options->buffer_size);
        fprintf(file, "    \"write_retries\": %u,\n", context->write_options->write_retries);
        fputs("    \"verify_mode\": ", file); json_string(file, verify_mode_name(context->write_options->verify_mode)); fputs(",\n", file);
        fprintf(file, "    \"sample_count\": %u,\n", context->write_options->sample_count);
        fprintf(file, "    \"direct_io\": %s\n", context->write_options->direct_io ? "true" : "false");
        fputs("  },\n", file);
    } else {
        fputs("  \"configuration\": null,\n", file);
    }

    if (context->write_result != NULL) {
        const df_write_result *r = context->write_result;
        fputs("  \"result\": {\n", file);
        fputs("    \"state\": ", file); json_string(file, r->final_state); fputs(",\n", file);
        fprintf(file, "    \"source_size\": %" PRIu64 ",\n", r->source_size);
        fprintf(file, "    \"bytes_written\": %" PRIu64 ",\n", r->bytes_written);
        fprintf(file, "    \"bytes_verified\": %" PRIu64 ",\n", r->bytes_verified);
        fprintf(file, "    \"write_retries\": %" PRIu64 ",\n", r->write_retries);
        fprintf(file, "    \"verification_mismatches\": %" PRIu64 ",\n", r->verification_mismatches);
        fprintf(file, "    \"source_hash_ms\": %.3f,\n", r->source_hash_ms);
        fprintf(file, "    \"write_ms\": %.3f,\n", r->write_ms);
        fprintf(file, "    \"flush_ms\": %.3f,\n", r->flush_ms);
        fprintf(file, "    \"verify_ms\": %.3f,\n", r->verify_ms);
        fprintf(file, "    \"total_ms\": %.3f,\n", r->total_ms);
        fprintf(file, "    \"write_mib_s\": %.3f,\n",
                r->write_ms > 0.0 ? ((double)r->bytes_written / 1048576.0) / (r->write_ms / 1000.0) : 0.0);
        fputs("    \"source_sha256\": ", file); json_string(file, source_hash); fputs(",\n", file);
        fputs("    \"target_sha256\": ", file); json_string(file, target_hash); fputs("\n  },\n", file);
    } else {
        fputs("  \"result\": null,\n", file);
    }

    if (context->fat32_layout != NULL) {
        const df_fat32_layout *l = context->fat32_layout;
        fputs("  \"fat32\": {\n", file);
        fprintf(file, "    \"bytes_per_sector\": %u,\n", l->bytes_per_sector);
        fprintf(file, "    \"sectors_per_cluster\": %u,\n", l->sectors_per_cluster);
        fprintf(file, "    \"reserved_sectors\": %u,\n", l->reserved_sectors);
        fprintf(file, "    \"fat_count\": %u,\n", l->fat_count);
        fprintf(file, "    \"sectors_per_fat\": %u,\n", l->sectors_per_fat);
        fprintf(file, "    \"partition_start_lba\": %u,\n", l->partition_start_lba);
        fprintf(file, "    \"partition_sectors\": %u,\n", l->partition_sectors);
        fprintf(file, "    \"data_clusters\": %u\n", l->data_clusters);
        fputs("  },\n", file);
    } else {
        fputs("  \"fat32\": null,\n", file);
    }

    fputs("  \"error\": {\"code\": ", file);
    json_string(file, context->error != NULL ? df_status_name(context->error->code) : "ok");
    fputs(", \"os_code\": ", file);
    fprintf(file, "%d", context->error != NULL ? context->error->os_code : 0);
    fputs(", \"message\": ", file);
    json_string(file, context->error != NULL ? context->error->message : "");
    fputs("}\n}\n", file);

    if (fclose(file) != 0) {
        df_error_set(error, DF_ERR_IO, 0, "could not flush report '%s'", path);
        return DF_ERR_IO;
    }
    return DF_OK;
}
