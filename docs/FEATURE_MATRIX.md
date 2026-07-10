DEADFLASH FEATURE MATRIX
========================

PURPOSE
-------

Separate implemented behavior from unimplemented ambition. A row may be called
competitive only when its code, tests, and benchmark evidence exist.

IMPLEMENTED IN 1.0.0 CANDIDATE
------------------------------

    RAW IMAGE WRITE
        Byte-for-byte IMG/ISO stream to file or supported physical target.

    SOURCE STABILITY CHECK
        Pre-write SHA-256 plus SHA-256 over the exact source bytes submitted to
        the writer.

    TARGET STALE-PLAN GUARD
        Target geometry and safety classification reduced to a confirmation
        token and reinspected before write access.

    OPERATION PLAN SEAL
        SHA-256 over source identity, target identity, and write policy.

    ATTESTED WRITE
        Recomputes the plan seal before calling the raw writer and rejects a
        stale or changed plan.

    CACHE-FLUSH BOUNDARY
        Explicit fsync or FlushFileBuffers before readback success.

    SAMPLE VERIFICATION
        Deterministic source-hash-seeded samples including first and final
        source regions.

    FULL VERIFICATION
        Full post-flush target SHA-256 over the exact source-length region.

    CHUNK PROOF
        Per-chunk SHA-256 records plus full source hash and binary Merkle root.

    EXACT MISMATCH LOCATION
        Reports the first absolute bad byte when source and target are present.

    EVIDENCE JSON
        Versioned result record with timings, counts, hashes, state, and error.

    NATIVE FAT32
        MBR plus FAT32 metadata for supported 512-byte-sector targets.

    REPRODUCIBLE BENCHMARK CONTRACT
        Separates write, flush, verify, proof, and corruption-location costs.

NOT IMPLEMENTED IN 1.0.0 CANDIDATE
----------------------------------

    Windows ISO extraction
    WIM splitting
    Windows To Go
    Windows 11 setup policy modification
    Persistence partitions
    GPT formatter
    exFAT formatter
    NTFS formatter
    UEFI boot validation
    ISO download service
    Bad-block destructive scan
    Fake-capacity qualification
    IOCP queued write engine
    Hardware-backed proof signatures

COMPETITIVE CLAIM RULE
----------------------

DEADFLASH may be described as stronger than Rufus only for an implemented,
tested, equally configured axis. The current intended edge is auditability:
plan-bound authorization, explicit result states, chunk proof, and exact
corruption location.

No row in the unimplemented section may appear in marketing, screenshots, or
release notes as though it exists.
