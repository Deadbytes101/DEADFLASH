DEADFLASH BENCHMARK PROTOCOL
============================

PURPOSE
-------

Produce reproducible evidence for throughput, completion time, verification,
and failure behavior. The protocol forbids comparing different correctness
levels as if they were equivalent.

COMPARISON CLASSES
------------------

    CLASS A: WRITE ONLY
    CLASS B: WRITE + FINAL CACHE FLUSH
    CLASS C: WRITE + DETERMINISTIC SAMPLE VERIFY
    CLASS D: WRITE + FULL READBACK VERIFY

A DEADFLASH Class D result cannot be compared directly with a Rufus write-only
result. Report both separately.

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
    tool versions
    verification class
    device starting condition

RUN ORDER
---------

Use at least five valid runs per tool and randomized AB/BA ordering.

Example for ten runs:

    Rufus, DEADFLASH, DEADFLASH, Rufus, DEADFLASH,
    Rufus, Rufus, DEADFLASH, Rufus, DEADFLASH

Do not discard failed runs. Preserve the failure report and classify it.

DEVICE CONDITIONING
-------------------

Flash translation layers retain state. Before every timed run, return the
medium to a documented condition. Acceptable methods include:

    - full-device deterministic overwrite followed by idle time
    - vendor secure erase when supported
    - a fixed preconditioning workload

The conditioning operation itself is not part of the timed result.

METRICS
-------

    source_hash_ms
    write_ms
    flush_ms
    verify_ms
    total_ms
    bytes_written
    bytes_verified
    write_mib_s
    retry_count
    verification_mismatch_count
    result_state

Report minimum, median, maximum, arithmetic mean, and standard deviation.
Median is the primary throughput statistic.

HARDWARE MATRIX
---------------

Minimum recommended matrix:

    A. Low-cost USB 2.0 flash drive
    B. Mainstream USB 3.x TLC flash drive
    C. High-performance USB 3.2 flash drive
    D. USB-to-SATA SSD bridge
    E. Known-fault or fake-capacity test device

IMAGE MATRIX
------------

    4 GiB incompressible raw image
    8 GiB zero-heavy image
    current Linux hybrid ISO
    current Windows installation ISO
    non-sector-aligned synthetic image
    image larger than target
    deliberately corrupted source

RUFUS COLLECTION
----------------

Rufus does not expose the same machine-readable benchmark interface. Preserve:

    - complete Rufus log
    - screenshot or screen recording showing start and completion
    - selected image hash
    - selected target identity
    - start and completion timestamps
    - Rufus version and settings

Do not infer flush or verification time if the tool does not expose it.
Mark unavailable fields as unavailable.

CLAIM LANGUAGE
--------------

Valid:

    "DEADFLASH had a 7.8% higher median write throughput on device B under
     Class B, five runs per tool, with identical image and USB port."

Invalid:

    "DEADFLASH is faster than Rufus."

A performance claim is not a reliability claim. A reliability claim requires
fault injection, readback, and power-cycle evidence.
