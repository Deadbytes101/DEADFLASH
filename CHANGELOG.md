DEADFLASH CHANGELOG
===================

1.0.0 - 2026-07-11
------------------

INITIAL EVIDENCE-FIRST RELEASE CANDIDATE.

    - Added raw image writer with explicit-offset I/O.
    - Added SHA-256 source hashing.
    - Added streaming hash over the exact unpadded bytes sent to the writer.
    - Added full and deterministic sampled readback verification.
    - Added target identity tokens and pre-write reinspection.
    - Added Linux block-device discovery and mount/system-disk checks.
    - Added Windows physical-drive discovery, volume locking, and dismounting.
    - Added explicit cache flush boundary.
    - Added machine-readable JSON evidence reports.
    - Added native MBR/FAT32 formatter and structural verifier.
    - Added deterministic file-backed benchmark command.
    - Added canonical SHA-256 operation-plan seals.
    - Added attested writes that reject changed source, target, or policy plans.
    - Added per-chunk SHA-256 proof manifests.
    - Added binary Merkle roots over chunk hashes.
    - Added exact first-mismatching-byte localization.
    - Added strict C17 builds with warnings as errors.
    - Added SHA-256, pipeline, identity, FAT32, attestation, corruption, and
      exact-offset proof tests.
    - Added native Win32 image-writer frontend with source preflight.
    - Added real phase-aware byte progress for hash, write, flush, and verify.
    - Added resizable keyboard-accessible operator layout and image drag/drop.
    - Added system-disk hiding plus source-versus-target capacity blocking.
    - Added final destructive confirmation containing source hash, target token,
      complete write policy, and cryptographic plan seal.
    - Added post-confirmation plan revalidation before the first media write.
    - Added attested GUI JSON evidence under Documents\DEADFLASH\Evidence.
    - Added Windows requireAdministrator, Common Controls v6, DPI, long-path,
      and 1.0.0 version resources.
