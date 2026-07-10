#include "deadflash/io.h"
#include "deadflash/sha256.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define DF_SAMPLE_BLOCK_SIZE (1024u * 1024u)
#define DF_DEFAULT_SAMPLE_COUNT 64u
#define DF_DEFAULT_WRITE_RETRIES 4u

static size_t df_round_up_size(size_t value, size_t alignment) {
    size_t remainder;
    if (alignment == 0) return value;
    remainder = value % alignment;
    if (remainder == 0) return value;
    if (value > SIZE_MAX - (alignment - remainder)) return 0;
    return value + alignment - remainder;
}

static uint64_t df_round_up_u64(uint64_t value, uint64_t alignment) {
    uint64_t remainder;
    if (alignment == 0) return value;
    remainder = value % alignment;
    if (remainder == 0) return value;
    if (value > UINT64_MAX - (alignment - remainder)) return 0;
    return value + alignment - remainder;
}

static void df_pipeline_file_reset(df_file *file) {
    memset(file, 0, sizeof(*file));
    file->handle = DF_INVALID_HANDLE;
    file->alignment = 512;
}

df_status df_hash_file_region(df_file *file, uint64_t length, size_t buffer_size,
                              uint8_t digest[32], df_error *error) {
    df_sha256_ctx hash;
    uint8_t *buffer;
    size_t alignment;
    size_t allocation_size;
    uint64_t offset = 0;
    if (file == NULL || digest == NULL || buffer_size == 0) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "invalid hash arguments");
        return DF_ERR_INVALID_ARGUMENT;
    }
    alignment = file->alignment != 0 ? file->alignment : 512u;
    allocation_size = df_round_up_size(buffer_size, alignment);
    if (allocation_size == 0) {
        df_error_set(error, DF_ERR_TOO_LARGE, 0, "hash buffer size overflow");
        return DF_ERR_TOO_LARGE;
    }
    buffer = (uint8_t *)df_aligned_alloc(alignment, allocation_size);
    if (buffer == NULL) {
        df_error_set(error, DF_ERR_NO_MEMORY, 0, "could not allocate hash buffer");
        return DF_ERR_NO_MEMORY;
    }
    df_sha256_init(&hash);
    while (offset < length) {
        size_t wanted = (size_t)((length - offset) < buffer_size ? (length - offset) : buffer_size);
        size_t request = wanted;
        size_t got = 0;
        df_status status;
        if (file->direct_io) {
            request = df_round_up_size(wanted, alignment);
            if (request == 0 || request > allocation_size) {
                df_aligned_free(buffer);
                df_error_set(error, DF_ERR_INTERNAL, 0, "invalid direct-I/O hash request");
                return DF_ERR_INTERNAL;
            }
        }
        status = df_read_at(file, buffer, request, offset, &got, error);
        if (status != DF_OK) {
            df_aligned_free(buffer);
            return status;
        }
        if (got < wanted) {
            df_aligned_free(buffer);
            df_error_set(error, DF_ERR_IO, 0, "unexpected end of file at offset %" PRIu64, offset);
            return DF_ERR_IO;
        }
        df_sha256_update(&hash, buffer, wanted);
        offset += wanted;
    }
    df_sha256_final(&hash, digest);
    df_aligned_free(buffer);
    return DF_OK;
}

static uint64_t df_prng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 2685821657736338717ULL;
}

static uint64_t df_seed_from_hash(const uint8_t digest[32]) {
    uint64_t seed = 0;
    size_t i;
    for (i = 0; i < 8; ++i) seed = (seed << 8) | digest[i];
    return seed != 0 ? seed : 0xdeadf1a5c0ffeeULL;
}

static df_status df_verify_sample_files(df_file *source, df_file *target,
                                        const uint8_t source_hash[32], unsigned sample_count,
                                        df_write_result *result, df_error *error) {
    uint8_t *left = NULL;
    uint8_t *right = NULL;
    uint64_t seed;
    unsigned i;
    size_t block_size;
    if (sample_count == 0) sample_count = DF_DEFAULT_SAMPLE_COUNT;
    block_size = source->size_bytes < DF_SAMPLE_BLOCK_SIZE ? (size_t)source->size_bytes : DF_SAMPLE_BLOCK_SIZE;
    if (block_size == 0) return DF_OK;
    left = (uint8_t *)df_aligned_alloc(4096, df_round_up_size(block_size, 4096));
    right = (uint8_t *)df_aligned_alloc(4096, df_round_up_size(block_size, 4096));
    if (left == NULL || right == NULL) {
        df_aligned_free(left);
        df_aligned_free(right);
        df_error_set(error, DF_ERR_NO_MEMORY, 0, "could not allocate sample verification buffers");
        return DF_ERR_NO_MEMORY;
    }
    seed = df_seed_from_hash(source_hash);
    for (i = 0; i < sample_count + 2u; ++i) {
        uint64_t max_offset = source->size_bytes - block_size;
        uint64_t offset;
        size_t got_left = 0, got_right = 0;
        df_status status;
        if (i == 0) offset = 0;
        else if (i == 1) offset = max_offset;
        else offset = max_offset == 0 ? 0 : df_prng_next(&seed) % (max_offset + 1u);
        status = df_read_at(source, left, block_size, offset, &got_left, error);
        if (status != DF_OK) goto fail;
        status = df_read_at(target, right, block_size, offset, &got_right, error);
        if (status != DF_OK) goto fail;
        if (got_left != block_size || got_right != block_size ||
            !df_constant_time_equal(left, right, block_size)) {
            result->verification_mismatches++;
            df_error_set(error, DF_ERR_VERIFY_MISMATCH, 0,
                         "sample verification mismatch at offset %" PRIu64, offset);
            status = DF_ERR_VERIFY_MISMATCH;
            goto fail;
        }
        result->bytes_verified += block_size;
    }
    df_aligned_free(left);
    df_aligned_free(right);
    return DF_OK;
fail:
    df_aligned_free(left);
    df_aligned_free(right);
    return error != NULL ? error->code : DF_ERR_VERIFY_MISMATCH;
}

df_status df_verify_image(const char *source_path, const char *target_path,
                          df_verify_mode mode, unsigned sample_count, size_t buffer_size,
                          df_write_result *result, df_error *error) {
    df_file source, target;
    df_timer timer;
    df_status status;
    uint8_t source_hash[32];
    uint8_t target_hash[32];
    df_error_clear(error);
    if (source_path == NULL || target_path == NULL || result == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "verify requires source, target, and result");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    df_pipeline_file_reset(&source);
    df_pipeline_file_reset(&target);
    if (buffer_size == 0) buffer_size = DF_DEFAULT_BUFFER_SIZE;
    status = df_open_source(source_path, &source, error);
    if (status != DF_OK) return status;
    status = df_open_source(target_path, &target, error);
    if (status != DF_OK) {
        df_close_file(&source);
        return status;
    }
    if (target.size_bytes != 0 && target.size_bytes < source.size_bytes) {
        df_error_set(error, DF_ERR_TOO_SMALL, 0, "target is smaller than source");
        status = DF_ERR_TOO_SMALL;
        goto out;
    }
    result->source_size = source.size_bytes;
    df_timer_start(&timer);
    status = df_hash_file_region(&source, source.size_bytes, buffer_size, source_hash, error);
    if (status != DF_OK) goto out;
    memcpy(result->source_sha256, source_hash, 32);
    result->source_hash_ms = df_timer_elapsed_ms(&timer);
    df_timer_start(&timer);
    if (mode == DF_VERIFY_NONE) {
        status = DF_OK;
    } else if (mode == DF_VERIFY_SAMPLE) {
        status = df_verify_sample_files(&source, &target, source_hash, sample_count, result, error);
    } else {
        status = df_hash_file_region(&target, source.size_bytes, buffer_size, target_hash, error);
        if (status == DF_OK) {
            memcpy(result->target_sha256, target_hash, 32);
            result->bytes_verified = source.size_bytes;
            if (!df_constant_time_equal(source_hash, target_hash, 32)) {
                result->verification_mismatches = 1;
                df_error_set(error, DF_ERR_VERIFY_MISMATCH, 0, "full verification hash mismatch");
                status = DF_ERR_VERIFY_MISMATCH;
            }
        }
    }
    result->verify_ms = df_timer_elapsed_ms(&timer);
    (void)snprintf(result->final_state, sizeof(result->final_state), "%s",
                   status == DF_OK ? (mode == DF_VERIFY_NONE ? "success_unverified" : "success_verified") : "verification_failed");
out:
    df_close_file(&source);
    df_close_file(&target);
    return status;
}

df_status df_write_image(const char *source_path, const char *target_path,
                         const df_write_options *options_in, df_target_info *target_info,
                         df_write_result *result, df_error *error) {
    df_write_options options;
    df_file source, target;
    df_target_info planned;
    df_timer total_timer, phase_timer;
    df_status status;
    df_sha256_ctx write_hash;
    uint8_t written_source_hash[32];
    uint8_t *buffer = NULL;
    size_t buffer_size;
    size_t alignment;
    uint64_t offset = 0;
    unsigned retries_default;
    df_error_clear(error);
    if (source_path == NULL || target_path == NULL || result == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0, "write requires source, target, and result");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(&options, 0, sizeof(options));
    if (options_in != NULL) options = *options_in;
    if (options.buffer_size == 0) options.buffer_size = DF_DEFAULT_BUFFER_SIZE;
    retries_default = options.write_retries == 0 ? DF_DEFAULT_WRITE_RETRIES : options.write_retries;
    options.write_retries = retries_default;
    if (options.sample_count == 0) options.sample_count = DF_DEFAULT_SAMPLE_COUNT;
    memset(result, 0, sizeof(*result));
    (void)snprintf(result->final_state, sizeof(result->final_state), "failed_before_write");
    df_pipeline_file_reset(&source);
    df_pipeline_file_reset(&target);
    df_timer_start(&total_timer);

    status = df_open_source(source_path, &source, error);
    if (status != DF_OK) return status;
    if (source.size_bytes == 0) {
        df_error_set(error, DF_ERR_TOO_SMALL, 0, "source image is empty");
        status = DF_ERR_TOO_SMALL;
        goto out;
    }
    result->source_size = source.size_bytes;
    status = df_inspect_target(target_path, &planned, error);
    if (status != DF_OK) goto out;
    if (target_info != NULL) *target_info = planned;
    if (planned.kind != DF_TARGET_REGULAR_FILE &&
        df_round_up_u64(source.size_bytes, planned.logical_sector_size) > planned.size_bytes) {
        df_error_set(error, DF_ERR_SOURCE_TOO_LARGE, 0,
                     "source image (%" PRIu64 ") exceeds target (%" PRIu64 ")",
                     source.size_bytes, planned.size_bytes);
        status = DF_ERR_SOURCE_TOO_LARGE;
        goto out;
    }

    df_timer_start(&phase_timer);
    status = df_hash_file_region(&source, source.size_bytes, options.buffer_size,
                                 result->source_sha256, error);
    if (status != DF_OK) goto out;
    result->source_hash_ms = df_timer_elapsed_ms(&phase_timer);

    status = df_open_target(target_path, &planned, &options, &target, error);
    if (status != DF_OK) goto out;
    if (!target.is_device && options.truncate_regular_file) {
        status = df_resize_regular_file(&target, source.size_bytes, error);
        if (status != DF_OK) goto out;
    }
    if (!target.is_device && target.size_bytes < source.size_bytes) {
        status = df_resize_regular_file(&target, source.size_bytes, error);
        if (status != DF_OK) goto out;
    }

    alignment = target.alignment > 4096u ? target.alignment : 4096u;
    buffer_size = df_round_up_size(options.buffer_size, alignment);
    if (buffer_size == 0) {
        df_error_set(error, DF_ERR_TOO_LARGE, 0, "buffer size overflow");
        status = DF_ERR_TOO_LARGE;
        goto out;
    }
    buffer = (uint8_t *)df_aligned_alloc(alignment, buffer_size);
    if (buffer == NULL) {
        df_error_set(error, DF_ERR_NO_MEMORY, 0, "could not allocate write buffer");
        status = DF_ERR_NO_MEMORY;
        goto out;
    }

    df_sha256_init(&write_hash);
    df_timer_start(&phase_timer);
    while (offset < source.size_bytes) {
        size_t wanted = (size_t)((source.size_bytes - offset) < buffer_size ?
                                 (source.size_bytes - offset) : buffer_size);
        size_t got = 0;
        size_t write_length = wanted;
        unsigned attempt;
        status = df_read_at(&source, buffer, wanted, offset, &got, error);
        if (status != DF_OK) goto out;
        if (got != wanted) {
            df_error_set(error, DF_ERR_IO, 0, "short source read at offset %" PRIu64, offset);
            status = DF_ERR_IO;
            goto out;
        }
        df_sha256_update(&write_hash, buffer, wanted);
        if (target.is_device && wanted % target.alignment != 0) {
            write_length = df_round_up_size(wanted, target.alignment);
            if (write_length == 0 || write_length > buffer_size) {
                df_error_set(error, DF_ERR_INTERNAL, 0, "invalid final aligned write size");
                status = DF_ERR_INTERNAL;
                goto out;
            }
            memset(buffer + wanted, 0, write_length - wanted);
        }
        for (attempt = 0; attempt < options.write_retries; ++attempt) {
            size_t written = 0;
            status = df_write_at(&target, buffer, write_length, offset, &written, error);
            if (status == DF_OK && written == write_length) break;
            result->write_retries++;
            if (status == DF_OK) {
                df_error_set(error, DF_ERR_IO, 0,
                             "short target write at offset %" PRIu64 ": %zu/%zu",
                             offset, written, write_length);
                status = DF_ERR_IO;
            }
        }
        if (status != DF_OK || attempt == options.write_retries) goto out;
        result->bytes_written += wanted;
        offset += wanted;
        (void)snprintf(result->final_state, sizeof(result->final_state), "failed_partial_media");
    }
    df_sha256_final(&write_hash, written_source_hash);
    if (!df_constant_time_equal(result->source_sha256, written_source_hash, 32)) {
        df_error_set(error, DF_ERR_IDENTITY_CHANGED, 0,
                     "source image changed between planning and the bytes written");
        status = DF_ERR_IDENTITY_CHANGED;
        goto out;
    }
    result->write_ms = df_timer_elapsed_ms(&phase_timer);

    df_timer_start(&phase_timer);
    status = df_flush(&target, error);
    result->flush_ms = df_timer_elapsed_ms(&phase_timer);
    if (status != DF_OK) goto out;
    df_close_file(&target);

    if (options.verify_mode != DF_VERIFY_NONE) {
        df_write_result verify_result;
        status = df_verify_image(source_path, target_path, options.verify_mode,
                                 options.sample_count, options.buffer_size,
                                 &verify_result, error);
        result->verify_ms = verify_result.verify_ms;
        result->bytes_verified = verify_result.bytes_verified;
        result->verification_mismatches = verify_result.verification_mismatches;
        memcpy(result->target_sha256, verify_result.target_sha256, 32);
        if (status != DF_OK) {
            (void)snprintf(result->final_state, sizeof(result->final_state), "verification_failed");
            goto out;
        }
        (void)snprintf(result->final_state, sizeof(result->final_state), "success_verified");
    } else {
        (void)snprintf(result->final_state, sizeof(result->final_state), "success_unverified");
    }
    status = DF_OK;
out:
    result->total_ms = df_timer_elapsed_ms(&total_timer);
    df_aligned_free(buffer);
    df_close_file(&source);
    df_close_file(&target);
    return status;
}
