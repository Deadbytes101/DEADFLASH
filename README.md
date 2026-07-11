DEADFLASH
=========

WRITE THE IMAGE. VERIFY THE TRUTH.

DEADFLASH is a native, evidence-first USB imaging and formatting utility.
It is built around explicit destructive plans, raw byte I/O, readback proof,
and reproducible measurements.

VERSION
-------

    1.0.0 FINAL

STATUS
------

    CORE + FINAL NATIVE WINDOWS 1.0.0 IMPLEMENTATION FROZEN

    IMPLEMENTATION HEAD : bc9f4f029c532abc5f23d2085d652b2e434467ae
    EVIDENCE HEAD       : aa7adb2c1c375daea5a4acc06b93c7db553a2beb

    - Raw IMG/ISO byte-for-byte writing
    - SHA-256 source hashing
    - Streaming hash of the exact bytes submitted to the writer
    - Full or deterministic sampled readback verification
    - Versioned JSON evidence reports
    - Hardware-bound physical-device confirmation tokens
    - Privacy-preserving serial SHA-256 instead of raw serial disclosure
    - Mounted-target and system-disk guards
    - Native MBR + FAT32 formatter
    - Deterministic benchmark command
    - Cryptographic operation-plan seals
    - Safety override policy bound into every plan seal
    - Per-chunk SHA-256 proof manifests
    - Binary Merkle root over all chunk hashes
    - Exact first-mismatching-byte localization
    - Final native Win32 physical-drive imaging frontend
    - Source preflight size + SHA-256
    - Real phase and byte progress from the storage core
    - Attested destructive confirmation and plan-seal evidence
    - Final-head MSVC + Windows SDK Release build: 7/7 + proof E2E
    - First physical USB write, flush, and full-readback checkpoint passed
    - Release publication remains evidence-gated

DEADFLASH does not claim full Rufus feature parity. Version 1.0.0 is the final
implementation of the destructive-storage core plus the native Windows operator
surface. Release publication remains gated by CI visibility, destructive fault
qualification, reconnect and power-cycle proof, and equal-class Rufus records.
It does not perform Windows ISO file extraction, WIM splitting, persistence
partitions, Windows To Go, or firmware boot emulation.

PHYSICAL QUALIFICATION CHECKPOINT
---------------------------------

The retained Windows GUI run wrote a 2,685,403,136-byte image to a removable
SanDisk 3.2Gen1 physical device, flushed the device, and completed full readback
verification. The run reported zero retries, zero mismatches, and identical
source and target SHA-256 values.

    bench/results/deadflash-evidence-20260711-155239.json

This checkpoint proves one complete physical write path. It does not replace the
remaining unplug, reconnect, power-cycle, fault-injection, multi-controller, and
Rufus equal-class release gates.

FINAL WINDOWS QUALIFICATION
---------------------------

The retained final-head Windows run used Visual Studio 18 2026 x64 and Windows
SDK 10.0.26100.0. The Release build passed warnings-as-errors, CTest passed 7/7,
and the proof/corruption E2E reached success_verified, success_proven, and exact
first-bad-byte localization.

    bench/results/msvc-qualification-20260711T090901Z.json
    bench/results/msvc-qualification-20260711T091840Z.json
    bench/results/msvc-e2e-20260711T091840Z.json

The first record retains the UI-only MSVC C4232 failure. The second and third
records retain the corrected final-head pass. Failed qualification records are
part of the evidence chain and are not deleted.

COMPETITIVE EDGE
----------------

DEADFLASH is designed to beat Rufus on one narrow and measurable axis:
auditable write authorization and post-write proof.

That statement is not a claim that DEADFLASH is the better all-purpose boot
media tool. It means the following implemented properties can be measured and
fault-tested directly:

    1. HARDWARE-BOUND TARGET TOKEN

       `deadflash inspect` computes target token v2 from the target path, kind,
       capacity, logical and physical sector sizes, read-only/system-disk state,
       bus, vendor, product, revision, and SHA-256 of the hardware serial when
       the device exposes one.

       Raw serial text is never printed or stored. The CLI and JSON evidence
       explicitly report SERIAL_BOUND, DESCRIPTOR_BOUND, or GEOMETRY_ONLY so a
       weak bridge identity is never represented as strong identity.

    2. PLAN SEAL

       `deadflash-proof seal` hashes a canonical operation plan containing the
       source SHA-256, source size, hardware-bound target token, target geometry,
       verification mode, sample count, buffer size, retry policy, direct-I/O
       mode, regular-file truncation policy, physical-device authorization,
       mounted-target override, and system-disk override.

       `deadflash-proof write --seal HEX` recomputes the complete plan before
       entering the destructive core. Any changed source, target fingerprint,
       safety override, or I/O policy rejects the old seal.

       The Windows GUI uses the same attestation contract. It displays the plan
       seal during final destructive confirmation and writes that seal into the
       JSON evidence record.

    3. CHUNK PROOF

       `deadflash-proof manifest` records the SHA-256 of every source chunk,
       the full source SHA-256, and a binary Merkle root. The root is an
       integrity summary, not a signature or authenticity certificate.

    4. EXACT DAMAGE LOCATION

       `deadflash-proof verify` validates the manifest, verifies source
       identity, compares every source/target chunk, and reports the first
       mismatching byte offset. A plain whole-image hash can say that data is
       wrong; this path also says where the first wrong byte is.

    5. NO FALSE VERIFIED SUCCESS

       Plan-seal mismatch, target-fingerprint change, source mutation, short
       write, flush failure, readback mismatch, proof mismatch, and evidence
       output failure remain distinct states. None may silently become a fully
       green proven run.

    6. REPRODUCIBLE COST

       The benchmark contract measures write, flush, verification, proof
       creation, proof verification, mismatch localization, CPU, and memory
       separately. Stronger correctness modes are never compared against a
       weaker mode under one generic throughput number.

These are implementation facts and test targets. Overall superiority over
Rufus remains invalid until raw physical-device benchmark files and
sacrificial-device fault results are published.

BUILD
-----

Linux:

    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure

Windows, Developer Command Prompt:

    cmake -S . -B build
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

The executables are:

    deadflash
    deadflash-proof
    deadflash-gui.exe        Windows only

WINDOWS GUI 1.0.0
-----------------

The GUI is a native C17 + Win32 executable. It does not use Electron, a browser
runtime, Qt, .NET, or a bundled renderer.

    build\Release\deadflash-gui.exe

The final operator surface provides:

    - IMG / ISO / BIN selection and drag-and-drop
    - source size and SHA-256 preflight on a worker thread
    - PhysicalDrive enumeration with Windows system disks hidden
    - vendor, product, capacity, bus, geometry, identity, and token overview
    - source-versus-target capacity gate
    - system-disk, read-only, and mounted-volume blocking
    - FULL, SAMPLED, or NO readback verification
    - fresh cryptographic operation-plan sealing
    - final confirmation containing source hash, target token, policy, and seal
    - post-confirmation plan revalidation before the first write
    - real HASH / WRITE / FLUSH / VERIFY byte progress
    - percent, byte counts, MiB/s, and elapsed phase time
    - determinate 100% completion presentation
    - resizable keyboard-accessible native layout
    - JSON evidence under Documents\DEADFLASH\Evidence
    - explicit VERIFIED, UNVERIFIED, EVIDENCE-FAILED, and FAILED result states

The GUI contains no system-disk override. The core remains authoritative for
every destructive safety decision.

See `docs/GUI.txt`.

FIRST SAFE RUN
--------------

Always inspect a physical device before writing it:

    deadflash list
    deadflash inspect /dev/sdX

Windows:

    deadflash list
    deadflash inspect \\.\PhysicalDrive3

Inspection prints the current identity strength, bus/vendor/product/revision,
a SHA-256 of the serial when available, and a short confirmation token. The
short token is a stale-target guard, not a digital certificate. A bridge that
exposes no useful descriptor is reported as GEOMETRY_ONLY and must be treated
with greater operator caution.

STANDARD VERIFIED WRITE
-----------------------

    deadflash write image.iso /dev/sdX \
        --allow-device \
        --confirm 0123456789abcdef \
        --verify full \
        --report run.json

ATTESTED WRITE + PROOF
----------------------

Generate a seal for the exact image, physical target, safety authorization,
and I/O policy:

    deadflash-proof seal image.iso /dev/sdX \
        --allow-device \
        --confirm 0123456789abcdef \
        --verify full \
        --buffer 32MiB

Execute only that sealed plan and emit a chunk proof. The safety flags must be
identical to the flags used during `seal`:

    deadflash-proof write image.iso /dev/sdX \
        --seal 64_HEX_CHARACTER_PLAN_SEAL \
        --allow-device \
        --confirm 0123456789abcdef \
        --verify full \
        --buffer 32MiB \
        --proof image.dfp \
        --chunk 4MiB

Recheck the proof after reconnecting or power-cycling the device:

    deadflash-proof verify image.dfp image.iso /dev/sdX

A harmless file-backed workflow:

    deadflash-proof seal image.iso target.img --verify full
    deadflash-proof write image.iso target.img \
        --seal 64_HEX_CHARACTER_PLAN_SEAL \
        --verify full \
        --proof image.dfp

FAT32 FORMAT
------------

    deadflash format-fat32 usb.img --size 512MiB --label DEADBYTE
    deadflash verify-fat32 usb.img

BENCHMARK
---------

Core write/flush/verify collector:

    ./scripts/benchmark-deadflash.sh \
        ./build/deadflash image.iso target.img 5 full

Plan seal, chunk proof, proof verification, and exact fault localization:

    python3 scripts/benchmark-proof.py \
        ./build/deadflash-proof image.iso target.img \
        --runs 5 \
        --chunk 4MiB \
        --buffer 32MiB \
        --fault-offset 1048593

The committed target-token-v2 file-backed sample is under `bench/results/`.
Its five proof runs all reached `success_proven`. Current medians are 56.819 ms
for plan sealing, 106.908 ms for proof creation, and 159.016 ms for proof
verification on a 6,291,579-byte image. One injected bad byte was reported at
the exact same absolute offset. This is a harness validation result, not a
USB-speed result and not a Rufus comparison.
