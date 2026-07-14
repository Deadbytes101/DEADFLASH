DEADFLASH 1.0.0
================

WRITE THE IMAGE. VERIFY THE TRUTH.

FIRST PUBLIC WINDOWS BINARY RELEASE.

DOWNLOAD
--------

    deadflash-gui.exe
        Native Win32 image writer verifier and removable-disk clean interface.

    deadflash.exe
        Command-line image writer inspector formatter and benchmark surface.

    deadflash-proof.exe
        Plan-seal proof-manifest verification and corruption-localization tool.

    DEADFLASH-1.0.0-windows-x64.zip
        Complete Windows x64 package with all executables documentation license
        SHA-256 checksums and retained qualification evidence.

    SHA256SUMS.txt
        SHA-256 values for every downloadable executable and package.

QUALIFIED BUILD
---------------

The release workflow builds directly from the tagged Git commit using MSVC with
warnings treated as errors. Publication is blocked unless all of these pass:

    - Native Windows GUI PE metadata manifest and branded USB icon validation
    - Fail-closed disk-clean contract test
    - CTest 7 / 7
    - File-backed attested write and full readback verification
    - Proof-manifest verification
    - Injected corruption detection at the exact byte offset

IMPLEMENTED SURFACE
-------------------

    - Raw IMG ISO and BIN physical-device writing
    - Source size and SHA-256 preflight
    - FULL deterministic SAMPLED or NONE readback policy
    - Cache-flush boundary before verification
    - Hardware-bound target identity token
    - System-disk mounted-target read-only and capacity guards
    - Canonical operation plan and SHA-256 plan seal
    - Live plan and target revalidation before first write
    - Versioned JSON evidence reports
    - Chunk proof manifests binary Merkle root and exact mismatch localization
    - Native MBR and FAT32 formatting plus structural verification
    - Integrated removable-USB whole-disk clean engine
    - Clean verification requiring RAW zero partitions and zero mounts

IMPORTANT
---------

DEADFLASH performs destructive storage operations. Confirm the physical target
path capacity identity token and operation plan before accepting a write or
clean confirmation. Use sacrificial media for qualification work.

The Windows executables are currently unsigned. Windows SmartScreen may display
an unknown-publisher warning. Verify the downloaded files against
SHA256SUMS.txt before running them.

This release does not claim full Rufus feature parity or measured overall
superiority over Rufus. It publishes the implemented evidence-first storage
surface and its retained qualification records.
