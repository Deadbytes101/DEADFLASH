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

    HARDWARE FINGERPRINT TOKEN V2
        Binds path, type, capacity, sector geometry, safety classification,
        bus/vendor/product/revision, and SHA-256 of hardware serial when exposed.
        Reports SERIAL_BOUND, DESCRIPTOR_BOUND, or GEOMETRY_ONLY explicitly.

    SERIAL PRIVACY
        Raw hardware serial text is neither printed nor stored. Only SHA-256 is
        used in target authorization and evidence.

    TARGET STALE-PLAN GUARD
        The live target fingerprint is reduced to a confirmation token and
        recomputed before write access.

    OPERATION PLAN SEAL
        SHA-256 over source identity, target fingerprint, write policy, and
        dangerous safety overrides.

    ATTESTED WRITE
        Recomputes the plan seal before calling the raw writer and rejects a
        stale source, target, safety policy, or I/O policy.

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
        Versioned result record with identity strength, hashed serial,
        configuration, timings, counts, hashes, state, and error.

    NATIVE FAT32
        MBR plus FAT32 metadata for supported 512-byte-sector targets.

    REPRODUCIBLE BENCHMARK CONTRACT
        Separates write, flush, verify, proof, and corruption-location costs.

    PHYSICAL USB QUALIFICATION HARNESS
        Fail-closed baseline, unplug, reconnect, and power-cycle record capture.
        The harness exists; physical pass records are still required.

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
    Fake-capacity automatic detection
    IOCP queued write engine
    Hardware-backed proof signatures
    Platform device-instance identity independent of target path

COMPETITIVE CLAIM RULE
----------------------

DEADFLASH may be described as stronger than Rufus only for an implemented,
tested, equally configured axis. The current intended edge is auditability:
hardware-bound target authorization when descriptors exist, plan-bound policy,
explicit result states, chunk proof, and exact corruption location.

No row in the unimplemented section may appear in marketing, screenshots, or
release notes as though it exists.
