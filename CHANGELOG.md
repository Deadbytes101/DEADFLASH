DEADFLASH CHANGELOG
===================

1.0.0 - 2026-07-11
------------------

INITIAL EVIDENCE-FIRST CORE RELEASE.

    - Added raw image writer with explicit-offset I/O.
    - Added SHA-256 source hashing.
    - Added full and deterministic sampled readback verification.
    - Added target identity tokens and pre-write reinspection.
    - Added Linux block-device discovery and mount/system-disk checks.
    - Added Windows physical-drive discovery, volume locking, and dismounting.
    - Added explicit cache flush boundary.
    - Added machine-readable JSON evidence reports.
    - Added native MBR/FAT32 formatter and structural verifier.
    - Added deterministic file-backed benchmark command.
    - Added strict C17 build with warnings as errors.
    - Added SHA-256, pipeline/corruption, and FAT32 tests.
