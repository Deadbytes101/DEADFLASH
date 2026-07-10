#ifndef DEADFLASH_PROOF_H
#define DEADFLASH_PROOF_H

#include "deadflash/io.h"

#define DF_PROOF_DEFAULT_CHUNK_SIZE (4u * 1024u * 1024u)
#define DF_PROOF_MAX_CHUNKS 4194304u

typedef struct df_proof_summary {
    uint64_t source_size;
    uint64_t chunk_size;
    uint64_t chunk_count;
    uint8_t source_sha256[32];
    uint8_t merkle_root[32];
} df_proof_summary;

typedef struct df_proof_result {
    uint64_t bytes_verified;
    uint64_t chunks_verified;
    uint64_t first_bad_chunk;
    uint64_t first_bad_offset;
    uint8_t expected_chunk_sha256[32];
    uint8_t actual_chunk_sha256[32];
    char final_state[48];
} df_proof_result;

df_status df_proof_create(const char *source_path, const char *manifest_path,
                          size_t chunk_size, df_proof_summary *summary,
                          df_error *error);
df_status df_proof_verify(const char *manifest_path, const char *source_path,
                          const char *target_path, df_proof_result *result,
                          df_error *error);

#endif
