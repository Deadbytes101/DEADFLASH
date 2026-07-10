DEADFLASH 1.0.0 RELEASE GATE
============================

SOFTWARE GATE
-------------

    [x] GCC warnings-as-errors build passes
    [x] Clang warnings-as-errors build passes
    [ ] MSVC warnings-as-errors build passes
    [x] Win32 conditional API-surface gate passes
    [x] ASan passes with GCC runtime
    [x] UBSan passes with GCC runtime
    [x] SHA-256 vectors pass
    [x] Pipeline full/sample verification passes
    [x] FAT32 structural validation passes
    [x] Target identity-change test passes
    [x] Hardware-fingerprint mutation tests pass
    [x] Plan-seal policy and safety mutation tests pass
    [x] Merkle manifest validation passes
    [x] Exact bad-byte offset test passes
    [x] Cross-platform file-backed E2E harness passes locally
    [ ] GitHub Actions checks are visible and green

The Win32 API-surface gate is not an MSVC substitute. It parses every `_WIN32`
conditional source path using strict Clang diagnostics and minimal API stubs,
but it does not link or execute Windows binaries.

PHYSICAL DEVICE GATE
--------------------

    [ ] Windows descriptor query validated on real USB hardware
    [ ] USB 2.0 flash drive write/flush/full verify
    [ ] USB 3.x flash drive write/flush/full verify
    [ ] USB-SATA bridge write/flush/full verify
    [ ] Identity strength and hashed serial recorded for every device
    [ ] Unplug during early write produces non-success partial-media evidence
    [ ] Unplug during late write produces non-success partial-media evidence
    [ ] Unplug during verify produces verification failure
    [ ] Reconnect then offline proof behaves as recorded
    [ ] Power-cycle then offline proof verify passes for baseline media
    [ ] Injected corruption reports exact byte offset
    [ ] Running system disk is rejected
    [ ] Mounted target is rejected or locked/dismounted as designed

EVIDENCE GATE
-------------

    [x] Local strict-build and sanitizer record retained
    [x] Win32 API-surface record retained
    [x] Raw file-backed proof benchmark CSV retained
    [x] File-backed fault-localization JSON retained
    [ ] Raw physical-device JSON evidence retained
    [ ] Raw physical benchmark CSV retained
    [ ] Failed physical runs retained
    [ ] Commit IDs and tool versions retained in every hardware record
    [ ] Rufus comparison uses equal correctness class
    [x] No broad superiority language appears without measured scope

TAG POLICY
----------

Do not create `v1.0.0` until every required unchecked box above has evidence
attached to the pull request or a release evidence issue. A compile-surface
pass, simulated file target, or successful benchmark harness cannot be promoted
into a physical-device or MSVC claim.
