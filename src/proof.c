#include "deadflash/proof.h"
#include "deadflash/sha256.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define DF_PROOF_MAGIC "DEADFLASH-PROOF-V1"
#define DF_PROOF_MAX_CHUNK_SIZE (1024u * 1024u * 1024u)

typedef struct df_proof_manifest {
    df_proof_summary summary;
    uint8_t *chunk_hashes;
    uint64_t *chunk_lengths;
} df_proof_manifest;

static void proof_file_reset(df_file *file) {
    memset(file, 0, sizeof(*file));
    file->handle = DF_INVALID_HANDLE;
    file->alignment = 512u;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex_decode_32(const char *text, uint8_t output[32]) {
    size_t i;
    if (text == NULL || strlen(text) != 64u) return false;
    for (i = 0; i < 32u; ++i) {
        int high = hex_nibble(text[i * 2u]);
        int low = hex_nibble(text[i * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static df_status read_exact(df_file *file, void *buffer, size_t length,
                            uint64_t offset, df_error *error) {
    size_t got = 0;
    df_status status = df_read_at(file, buffer, length, offset, &got, error);
    if (status != DF_OK) return status;
    if (got != length) {
        df_error_set(error, DF_ERR_IO, 0,
                     "short proof read at offset %" PRIu64 ": %zu/%zu",
                     offset, got, length);
        return DF_ERR_IO;
    }
    return DF_OK;
}

static void merkle_parent(const uint8_t left[32], const uint8_t right[32],
                          uint8_t output[32]) {
    uint8_t block[65];
    block[0] = 0x01u;
    memcpy(block + 1u, left, 32u);
    memcpy(block + 33u, right, 32u);
    df_sha256_buffer(block, sizeof(block), output);
}

static df_status merkle_root(const uint8_t *leaves, uint64_t leaf_count,
                             uint8_t output[32], df_error *error) {
    uint8_t *level;
    uint64_t count;
    if (leaves == NULL || output == NULL || leaf_count == 0u ||
        leaf_count > DF_PROOF_MAX_CHUNKS) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "invalid Merkle leaf set");
        return DF_ERR_INVALID_ARGUMENT;
    }
    if (leaf_count > (uint64_t)(SIZE_MAX / 32u)) {
        df_error_set(error, DF_ERR_TOO_LARGE, 0,
                     "Merkle tree allocation overflow");
        return DF_ERR_TOO_LARGE;
    }
    level = (uint8_t *)malloc((size_t)leaf_count * 32u);
    if (level == NULL) {
        df_error_set(error, DF_ERR_NO_MEMORY, 0,
                     "could not allocate Merkle tree");
        return DF_ERR_NO_MEMORY;
    }
    memcpy(level, leaves, (size_t)leaf_count * 32u);
    count = leaf_count;
    while (count > 1u) {
        uint64_t out_count = (count + 1u) / 2u;
        uint64_t i;
        for (i = 0; i < out_count; ++i) {
            const uint8_t *left = level + (size_t)(i * 2u) * 32u;
            const uint8_t *right =
                (i * 2u + 1u < count) ? left + 32u : left;
            merkle_parent(left, right, level + (size_t)i * 32u);
        }
        count = out_count;
    }
    memcpy(output, level, 32u);
    free(level);
    return DF_OK;
}

static void manifest_free(df_proof_manifest *manifest) {
    if (manifest == NULL) return;
    free(manifest->chunk_hashes);
    free(manifest->chunk_lengths);
    memset(manifest, 0, sizeof(*manifest));
}

static df_status manifest_load(const char *path,
                               df_proof_manifest *manifest,
                               df_error *error) {
    FILE *file = NULL;
    char key[64];
    char text[128];
    uint64_t i;
    uint64_t expected_count;
    uint8_t computed_root[32];
    df_status status = DF_ERR_FORMAT;

    if (path == NULL || manifest == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "proof manifest path is required");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(manifest, 0, sizeof(*manifest));
    file = fopen(path, "rb");
    if (file == NULL) {
        df_error_set(error, DF_ERR_OPEN, errno,
                     "open proof manifest '%s'", path);
        return DF_ERR_OPEN;
    }
    if (fscanf(file, "%63s", text) != 1 ||
        strcmp(text, DF_PROOF_MAGIC) != 0) {
        df_error_set(error, DF_ERR_FORMAT, 0,
                     "invalid proof manifest magic");
        goto out;
    }
    if (fscanf(file, "%63s %" SCNu64, key,
               &manifest->summary.source_size) != 2 ||
        strcmp(key, "source_size") != 0) {
        df_error_set(error, DF_ERR_FORMAT, 0, "missing source_size");
        goto out;
    }
    if (fscanf(file, "%63s %" SCNu64, key,
               &manifest->summary.chunk_size) != 2 ||
        strcmp(key, "chunk_size") != 0) {
        df_error_set(error, DF_ERR_FORMAT, 0, "missing chunk_size");
        goto out;
    }
    if (fscanf(file, "%63s %" SCNu64, key,
               &manifest->summary.chunk_count) != 2 ||
        strcmp(key, "chunk_count") != 0) {
        df_error_set(error, DF_ERR_FORMAT, 0, "missing chunk_count");
        goto out;
    }
    if (fscanf(file, "%63s %127s", key, text) != 2 ||
        strcmp(key, "source_sha256") != 0 ||
        !hex_decode_32(text, manifest->summary.source_sha256)) {
        df_error_set(error, DF_ERR_FORMAT, 0, "invalid source_sha256");
        goto out;
    }
    if (manifest->summary.source_size == 0u ||
        manifest->summary.chunk_size == 0u ||
        manifest->summary.chunk_size > DF_PROOF_MAX_CHUNK_SIZE ||
        manifest->summary.chunk_count == 0u ||
        manifest->summary.chunk_count > DF_PROOF_MAX_CHUNKS) {
        df_error_set(error, DF_ERR_FORMAT, 0,
                     "proof manifest limits are invalid");
        goto out;
    }
    expected_count =
        (manifest->summary.source_size + manifest->summary.chunk_size - 1u) /
        manifest->summary.chunk_size;
    if (expected_count != manifest->summary.chunk_count) {
        df_error_set(error, DF_ERR_FORMAT, 0,
                     "proof chunk_count does not match source size");
        goto out;
    }
    if (manifest->summary.chunk_count > (uint64_t)(SIZE_MAX / 32u) ||
        manifest->summary.chunk_count >
            (uint64_t)(SIZE_MAX / sizeof(uint64_t))) {
        df_error_set(error, DF_ERR_TOO_LARGE, 0,
                     "proof manifest allocation overflow");
        status = DF_ERR_TOO_LARGE;
        goto out;
    }
    manifest->chunk_hashes =
        (uint8_t *)malloc((size_t)manifest->summary.chunk_count * 32u);
    manifest->chunk_lengths =
        (uint64_t *)calloc((size_t)manifest->summary.chunk_count,
                           sizeof(uint64_t));
    if (manifest->chunk_hashes == NULL || manifest->chunk_lengths == NULL) {
        df_error_set(error, DF_ERR_NO_MEMORY, 0,
                     "could not allocate proof manifest");
        status = DF_ERR_NO_MEMORY;
        goto out;
    }
    for (i = 0; i < manifest->summary.chunk_count; ++i) {
        uint64_t index = 0;
        uint64_t length = 0;
        uint64_t expected_length = manifest->summary.chunk_size;
        if (i + 1u == manifest->summary.chunk_count) {
            expected_length =
                manifest->summary.source_size - i * manifest->summary.chunk_size;
        }
        if (fscanf(file, "%63s %" SCNu64 " %" SCNu64 " %127s",
                   key, &index, &length, text) != 4 ||
            strcmp(key, "chunk") != 0 || index != i ||
            length != expected_length ||
            !hex_decode_32(text,
                           manifest->chunk_hashes + (size_t)i * 32u)) {
            df_error_set(error, DF_ERR_FORMAT, 0,
                         "invalid proof chunk record at index %" PRIu64, i);
            goto out;
        }
        manifest->chunk_lengths[i] = length;
    }
    if (fscanf(file, "%63s %127s", key, text) != 2 ||
        strcmp(key, "merkle_root") != 0 ||
        !hex_decode_32(text, manifest->summary.merkle_root)) {
        df_error_set(error, DF_ERR_FORMAT, 0, "invalid merkle_root");
        goto out;
    }
    if (fscanf(file, "%63s", text) != 1 || strcmp(text, "END") != 0) {
        df_error_set(error, DF_ERR_FORMAT, 0,
                     "proof manifest is not terminated");
        goto out;
    }
    status = merkle_root(manifest->chunk_hashes,
                         manifest->summary.chunk_count,
                         computed_root, error);
    if (status != DF_OK) goto out;
    if (!df_constant_time_equal(computed_root,
                                manifest->summary.merkle_root, 32u)) {
        df_error_set(error, DF_ERR_FORMAT, 0,
                     "proof manifest Merkle root mismatch");
        status = DF_ERR_FORMAT;
        goto out;
    }
    status = DF_OK;
out:
    fclose(file);
    if (status != DF_OK) manifest_free(manifest);
    return status;
}

df_status df_proof_create(const char *source_path,
                          const char *manifest_path,
                          size_t chunk_size,
                          df_proof_summary *summary,
                          df_error *error) {
    df_file source;
    df_sha256_ctx source_hash;
    df_proof_summary local;
    uint8_t *buffer = NULL;
    uint8_t *chunk_hashes = NULL;
    FILE *file = NULL;
    uint64_t offset = 0;
    uint64_t index = 0;
    df_status status = DF_OK;
    char source_hex[65];
    char merkle_hex[65];

    df_error_clear(error);
    if (source_path == NULL || manifest_path == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "source and proof paths are required");
        return DF_ERR_INVALID_ARGUMENT;
    }
    if (chunk_size == 0u) chunk_size = DF_PROOF_DEFAULT_CHUNK_SIZE;
    if (chunk_size < 4096u || chunk_size > DF_PROOF_MAX_CHUNK_SIZE) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "proof chunk size must be between 4 KiB and 1 GiB");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(&local, 0, sizeof(local));
    proof_file_reset(&source);
    status = df_open_source(source_path, &source, error);
    if (status != DF_OK) return status;
    if (source.size_bytes == 0u) {
        df_error_set(error, DF_ERR_TOO_SMALL, 0,
                     "cannot create proof for an empty source");
        status = DF_ERR_TOO_SMALL;
        goto out;
    }
    local.source_size = source.size_bytes;
    local.chunk_size = chunk_size;
    local.chunk_count =
        (source.size_bytes + (uint64_t)chunk_size - 1u) /
        (uint64_t)chunk_size;
    if (local.chunk_count > DF_PROOF_MAX_CHUNKS ||
        local.chunk_count > (uint64_t)(SIZE_MAX / 32u)) {
        df_error_set(error, DF_ERR_TOO_LARGE, 0,
                     "proof would exceed the maximum chunk count");
        status = DF_ERR_TOO_LARGE;
        goto out;
    }
    buffer = (uint8_t *)malloc(chunk_size);
    chunk_hashes = (uint8_t *)malloc((size_t)local.chunk_count * 32u);
    if (buffer == NULL || chunk_hashes == NULL) {
        df_error_set(error, DF_ERR_NO_MEMORY, 0,
                     "could not allocate proof buffers");
        status = DF_ERR_NO_MEMORY;
        goto out;
    }
    df_sha256_init(&source_hash);
    while (offset < source.size_bytes) {
        size_t length =
            (size_t)((source.size_bytes - offset) < (uint64_t)chunk_size
                         ? (source.size_bytes - offset)
                         : (uint64_t)chunk_size);
        status = read_exact(&source, buffer, length, offset, error);
        if (status != DF_OK) goto out;
        df_sha256_update(&source_hash, buffer, length);
        df_sha256_buffer(buffer, length,
                         chunk_hashes + (size_t)index * 32u);
        offset += length;
        index++;
    }
    df_sha256_final(&source_hash, local.source_sha256);
    status = merkle_root(chunk_hashes, local.chunk_count,
                         local.merkle_root, error);
    if (status != DF_OK) goto out;

    file = fopen(manifest_path, "wb");
    if (file == NULL) {
        df_error_set(error, DF_ERR_OPEN, errno,
                     "create proof manifest '%s'", manifest_path);
        status = DF_ERR_OPEN;
        goto out;
    }
    df_hex_encode(local.source_sha256, 32u, source_hex);
    df_hex_encode(local.merkle_root, 32u, merkle_hex);
    if (fprintf(file,
                "%s\nsource_size %" PRIu64
                "\nchunk_size %" PRIu64
                "\nchunk_count %" PRIu64
                "\nsource_sha256 %s\n",
                DF_PROOF_MAGIC, local.source_size, local.chunk_size,
                local.chunk_count, source_hex) < 0) {
        df_error_set(error, DF_ERR_IO, errno,
                     "write proof manifest header");
        status = DF_ERR_IO;
        goto out;
    }
    for (index = 0; index < local.chunk_count; ++index) {
        uint64_t length = local.chunk_size;
        char chunk_hex[65];
        if (index + 1u == local.chunk_count) {
            length = local.source_size - index * local.chunk_size;
        }
        df_hex_encode(chunk_hashes + (size_t)index * 32u,
                      32u, chunk_hex);
        if (fprintf(file, "chunk %" PRIu64 " %" PRIu64 " %s\n",
                    index, length, chunk_hex) < 0) {
            df_error_set(error, DF_ERR_IO, errno,
                         "write proof manifest chunk");
            status = DF_ERR_IO;
            goto out;
        }
    }
    if (fprintf(file, "merkle_root %s\nEND\n", merkle_hex) < 0 ||
        fflush(file) != 0) {
        df_error_set(error, DF_ERR_IO, errno,
                     "finalize proof manifest");
        status = DF_ERR_IO;
        goto out;
    }
    if (summary != NULL) *summary = local;
out:
    if (file != NULL && fclose(file) != 0 && status == DF_OK) {
        df_error_set(error, DF_ERR_IO, errno,
                     "close proof manifest");
        status = DF_ERR_IO;
    }
    if (status != DF_OK && manifest_path != NULL)
        (void)remove(manifest_path);
    free(buffer);
    free(chunk_hashes);
    df_close_file(&source);
    return status;
}

df_status df_proof_verify(const char *manifest_path,
                          const char *source_path,
                          const char *target_path,
                          df_proof_result *result,
                          df_error *error) {
    df_proof_manifest manifest;
    df_file source;
    df_file target;
    uint8_t *source_buffer = NULL;
    uint8_t *target_buffer = NULL;
    df_sha256_ctx source_hash;
    uint8_t source_digest[32];
    uint64_t index;
    df_status status;

    df_error_clear(error);
    if (manifest_path == NULL || source_path == NULL ||
        target_path == NULL || result == NULL) {
        df_error_set(error, DF_ERR_INVALID_ARGUMENT, 0,
                     "proof verify requires manifest, source, target, and result");
        return DF_ERR_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    result->first_bad_chunk = UINT64_MAX;
    result->first_bad_offset = UINT64_MAX;
    (void)snprintf(result->final_state, sizeof(result->final_state),
                   "failed_before_verify");
    memset(&manifest, 0, sizeof(manifest));
    proof_file_reset(&source);
    proof_file_reset(&target);

    status = manifest_load(manifest_path, &manifest, error);
    if (status != DF_OK) return status;
    status = df_open_source(source_path, &source, error);
    if (status != DF_OK) goto out;
    status = df_open_source(target_path, &target, error);
    if (status != DF_OK) goto out;
    if (source.size_bytes != manifest.summary.source_size) {
        df_error_set(error, DF_ERR_IDENTITY_CHANGED, 0,
                     "source size differs from proof manifest");
        status = DF_ERR_IDENTITY_CHANGED;
        goto out;
    }
    if (target.size_bytes != 0u &&
        target.size_bytes < manifest.summary.source_size) {
        df_error_set(error, DF_ERR_TOO_SMALL, 0,
                     "target is smaller than proof source");
        status = DF_ERR_TOO_SMALL;
        goto out;
    }
    if (manifest.summary.chunk_size > (uint64_t)SIZE_MAX) {
        df_error_set(error, DF_ERR_TOO_LARGE, 0,
                     "proof chunk size exceeds platform limit");
        status = DF_ERR_TOO_LARGE;
        goto out;
    }
    source_buffer =
        (uint8_t *)malloc((size_t)manifest.summary.chunk_size);
    target_buffer =
        (uint8_t *)malloc((size_t)manifest.summary.chunk_size);
    if (source_buffer == NULL || target_buffer == NULL) {
        df_error_set(error, DF_ERR_NO_MEMORY, 0,
                     "could not allocate proof verify buffers");
        status = DF_ERR_NO_MEMORY;
        goto out;
    }
    df_sha256_init(&source_hash);
    for (index = 0; index < manifest.summary.chunk_count; ++index) {
        uint64_t offset = index * manifest.summary.chunk_size;
        size_t length = (size_t)manifest.chunk_lengths[index];
        uint8_t source_chunk_hash[32];
        uint8_t target_chunk_hash[32];
        size_t byte_index;

        status = read_exact(&source, source_buffer, length, offset, error);
        if (status != DF_OK) goto out;
        df_sha256_update(&source_hash, source_buffer, length);
        df_sha256_buffer(source_buffer, length, source_chunk_hash);
        if (!df_constant_time_equal(
                source_chunk_hash,
                manifest.chunk_hashes + (size_t)index * 32u, 32u)) {
            result->first_bad_chunk = index;
            result->first_bad_offset = offset;
            (void)snprintf(result->final_state,
                           sizeof(result->final_state), "source_changed");
            df_error_set(error, DF_ERR_IDENTITY_CHANGED, 0,
                         "source chunk %" PRIu64
                         " differs from proof manifest", index);
            status = DF_ERR_IDENTITY_CHANGED;
            goto out;
        }
        status = read_exact(&target, target_buffer, length, offset, error);
        if (status != DF_OK) goto out;
        df_sha256_buffer(target_buffer, length, target_chunk_hash);
        if (!df_constant_time_equal(source_chunk_hash,
                                    target_chunk_hash, 32u)) {
            result->first_bad_chunk = index;
            memcpy(result->expected_chunk_sha256,
                   source_chunk_hash, 32u);
            memcpy(result->actual_chunk_sha256,
                   target_chunk_hash, 32u);
            result->first_bad_offset = offset;
            for (byte_index = 0; byte_index < length; ++byte_index) {
                if (source_buffer[byte_index] != target_buffer[byte_index]) {
                    result->first_bad_offset =
                        offset + (uint64_t)byte_index;
                    break;
                }
            }
            (void)snprintf(result->final_state,
                           sizeof(result->final_state), "target_mismatch");
            df_error_set(error, DF_ERR_VERIFY_MISMATCH, 0,
                         "target mismatch at byte offset %" PRIu64,
                         result->first_bad_offset);
            status = DF_ERR_VERIFY_MISMATCH;
            goto out;
        }
        result->chunks_verified++;
        result->bytes_verified += length;
    }
    df_sha256_final(&source_hash, source_digest);
    if (!df_constant_time_equal(source_digest,
                                manifest.summary.source_sha256, 32u)) {
        (void)snprintf(result->final_state,
                       sizeof(result->final_state), "source_changed");
        df_error_set(error, DF_ERR_IDENTITY_CHANGED, 0,
                     "source SHA-256 differs from proof manifest");
        status = DF_ERR_IDENTITY_CHANGED;
        goto out;
    }
    (void)snprintf(result->final_state,
                   sizeof(result->final_state), "success_proven");
    status = DF_OK;
out:
    free(source_buffer);
    free(target_buffer);
    df_close_file(&source);
    df_close_file(&target);
    manifest_free(&manifest);
    return status;
}
