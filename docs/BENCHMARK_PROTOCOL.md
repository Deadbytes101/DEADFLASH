DEADFLASH BENCHMARK PROTOCOL
============================

PURPOSE
-------

Produce reproducible evidence for throughput, completion time, verification,
proof generation, mismatch localization, and failure behavior. The protocol
forbids comparing different correctness levels as if they were equivalent.

COMPARISON CLASSES
------------------

    CLASS A: WRITE ONLY
    CLASS B: WRITE + FINAL CACHE FLUSH
    CLASS C: WRITE + DETERMINISTIC SAMPLE VERIFY
    CLASS D: WRITE + FULL READBACK VERIFY
    CLASS E: CLASS D + PLAN SEAL + CHUNK PROOF CREATION
    CLASS F: OFFLINE CHUNK PROOF VERIFICATION
    CLASS G: CORRUPTION DETECTION + EXACT BYTE LOCALIZATION

A DEADFLASH Class D or E result cannot be compared directly with a Rufus
write-only result. Report each class separately.

Class E measures the cost of the stronger DEADFLASH evidence path. It is not
valid to hide plan-hash or proof-generation time outside the total.

FIXED VARIABLES
---------------

    physical USB device and firmware
    USB port and controller
    extension cable or hub state
    source image and SHA-256
    source storage device
    operating-system build
    power plan
    antivirus configuration
    tool versions and commit IDs
    verification class
    proof chunk size
    write buffer size
    direct-I/O policy
    device starting condition
    ambient and device temperature window

RUN ORDER
---------

Use at least five valid runs per tool and randomized AB/BA ordering.

Example for ten runs:

    Rufus, DEADFLASH, DEADFLASH, Rufus, DEADFLASH,
    Rufus, Rufus, DEADFLASH, Rufus, DEADFLASH

Do not discard failed runs. Preserve the failure report and classify it.
Do not remove an outlier unless the exclusion rule was written before testing
and the raw excluded record remains published.

DEVICE CONDITIONING
-------------------

Flash translation layers retain state. Before every timed run, return the
medium to a documented condition. Acceptable methods include:

    - full-device deterministic overwrite followed by fixed idle time
    - vendor secure erase when supported
    - fixed preconditioning workload

The conditioning operation itself is not part of the timed result.

CORE METRICS
------------

    plan_seal_ms
    source_hash_ms
    write_ms
    flush_ms
    verify_ms
    proof_create_ms
    proof_verify_ms
    mismatch_localization_ms
    total_ms
    bytes_written
    bytes_verified
    write_mib_s
    verified_mib_s
    proof_mib_s
    retry_count
    verification_mismatch_count
    result_state
    peak_working_set_bytes
    cpu_user_ms
    cpu_kernel_ms

PROOF CORRECTNESS METRICS
-------------------------

    manifest_source_sha256_equal
    manifest_merkle_root_equal
    source_chunk_hashes_equal
    target_chunk_hashes_equal
    injected_bad_offset
    reported_first_bad_offset
    first_bad_offset_accuracy

`first_bad_offset_accuracy` is PASS only when the absolute reported byte offset
is exactly equal to the injected offset. Reporting only a mismatching chunk is
not an exact-location pass.

STATISTICS
----------

Report minimum, median, maximum, arithmetic mean, standard deviation, and run
count. Median is the primary throughput statistic. For failure latency, report
time to reject and whether any target byte was written before rejection.

HARDWARE MATRIX
---------------

Minimum recommended matrix:

    A. Low-cost USB 2.0 flash drive
    B. Mainstream USB 3.x TLC flash drive
    C. High-performance USB 3.2 flash drive
    D. USB-to-SATA SSD bridge
    E. Known-fault or fake-capacity test device
    F. Write-protected physical device

IMAGE MATRIX
------------

    4 GiB incompressible raw image
    8 GiB zero-heavy image
    current Linux hybrid ISO
    current Windows installation ISO
    non-sector-aligned synthetic image
    image larger than target
    deliberately corrupted source
    source modified after seal generation
    target changed after seal generation

FAULT MATRIX
------------

    stale plan seal
    changed verification mode after seal
    changed buffer policy after seal
    source mutation before write
    source mutation during write
    target identity or size change
    short write
    flush failure
    unplug during write
    unplug during verification
    single-byte target corruption
    first-byte and final-byte corruption
    manifest chunk-line corruption
    manifest Merkle-root corruption
    power cycle before offline proof verification

No failure may be classified as verified or proven success.

RUFUS COLLECTION
----------------

Preserve:

    - complete Rufus log
    - screenshot or recording showing settings, start, and completion
    - selected image hash
    - selected target identity
    - start and completion timestamps
    - Rufus version and settings

Do not infer flush, verification, or proof time if the tool does not expose it.
Mark unavailable fields as unavailable. Missing telemetry is not converted into
zero milliseconds.

CLAIM LANGUAGE
--------------

Valid:

    "DEADFLASH had a 7.8% higher median write throughput on device B under
     Class B, five runs per tool, with identical image and USB port."

    "DEADFLASH rejected all 20 stale-plan mutations before entering its raw
     writer; the test records and commit ID are published."

    "DEADFLASH located all 100 injected one-byte corruptions at the exact
     absolute byte offset under Class G."

Invalid:

    "DEADFLASH is faster than Rufus."

    "DEADFLASH is safer because it uses a Merkle tree."

A performance claim is not a reliability claim. A reliability claim requires
fault injection, readback, power-cycle evidence, and raw result files.
