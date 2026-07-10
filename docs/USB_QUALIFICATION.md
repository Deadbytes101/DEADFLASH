DEADFLASH PHYSICAL USB QUALIFICATION
====================================

VERSION: 1.0.0 CANDIDATE

PURPOSE
-------

This procedure qualifies the destructive physical-device path. It is not a
simulation and it must use sacrificial media containing no valuable data.

The qualification harness records every command, return code, stdout, stderr,
timing value, target token, image SHA-256, and final state as JSON.

HARD RULES
----------

    1. Never test on a system disk.
    2. Never test on media containing required data.
    3. Remove unrelated removable drives before the run.
    4. Run from an elevated Administrator terminal on Windows.
    5. Inspect the target immediately before each destructive phase.
    6. Do not reuse a token after reconnecting the device.
    7. Commit failed records as well as successful records.
    8. Do not create the v1.0.0 tag until the complete matrix passes.

WINDOWS PREPARATION
-------------------

Build Release binaries, then inspect the sacrificial drive:

    build\Release\deadflash.exe list
    build\Release\deadflash.exe inspect \\.\PhysicalDrive3

Copy the exact CONFIRMATION_TOKEN printed by inspect.

The qualification script requires two independent destructive gates:

    --execute
    --destroy-phrase "DESTROY:\\.\PhysicalDrive3:TOKEN"

A missing or incorrect phrase aborts before the first write.

BASELINE WRITE + FULL READBACK + PROOF
--------------------------------------

    python scripts\qualify-usb.py baseline ^
        --deadflash build\Release\deadflash.exe ^
        --proof build\Release\deadflash-proof.exe ^
        --image C:\images\qualification.img ^
        --target \\.\PhysicalDrive3 ^
        --token TOKEN ^
        --output-dir bench\hardware\DEVICE-A ^
        --verify full ^
        --buffer 32MiB ^
        --chunk 4MiB ^
        --execute ^
        --destroy-phrase "DESTROY:\\.\PhysicalDrive3:TOKEN"

PASS REQUIREMENTS
-----------------

    write state       = success_verified
    proof state       = success_proven
    offline proof     = success_proven
    process exit      = 0
    raw JSON record   = retained
    proof manifest    = retained

UNPLUG DURING WRITE
-------------------

Use an image large enough that the write cannot complete before the countdown.
Inspect the target again and use the new token.

    python scripts\qualify-usb.py unplug ^
        --deadflash build\Release\deadflash.exe ^
        --proof build\Release\deadflash-proof.exe ^
        --image C:\images\qualification-large.img ^
        --target \\.\PhysicalDrive3 ^
        --token TOKEN ^
        --output-dir bench\hardware\DEVICE-A ^
        --unplug-delay 3 ^
        --timeout 60 ^
        --execute ^
        --destroy-phrase "DESTROY:\\.\PhysicalDrive3:TOKEN"

Unplug the device only when the script prints:

    UNPLUG NOW.

PASS REQUIREMENTS
-----------------

    process exit      != 0
    final state       != success_verified
    final state       != success_unverified
    fault record      = retained

The expected result is usually failed_partial_media, device-loss I/O failure,
or verification failure. The exact operating-system error may vary by bridge
and controller. A successful exit is a failed test.

POWER-CYCLE / RECONNECT VERIFY
------------------------------

Reconnect the device, inspect it again, and use the new current token and path.
Then verify the baseline proof manifest:

    python scripts\qualify-usb.py verify ^
        --deadflash build\Release\deadflash.exe ^
        --proof build\Release\deadflash-proof.exe ^
        --image C:\images\qualification.img ^
        --target \\.\PhysicalDrive3 ^
        --token NEW_TOKEN ^
        --manifest bench\hardware\DEVICE-A\baseline.dfp ^
        --output-dir bench\hardware\DEVICE-A ^
        --execute ^
        --destroy-phrase "DESTROY:\\.\PhysicalDrive3:NEW_TOKEN"

For a completed baseline write, the required state is success_proven.
For intentionally interrupted media, target_mismatch is acceptable only when
it is recorded as failure evidence and never mislabeled as successful media.

MINIMUM HARDWARE MATRIX
-----------------------

    DEVICE A  low-cost USB 2.0 flash drive
    DEVICE B  mainstream USB 3.x TLC flash drive
    DEVICE C  high-performance USB 3.2 flash drive
    DEVICE D  USB-to-SATA SSD bridge
    DEVICE E  known-fault or fake-capacity device

For each device record:

    manufacturer and model
    advertised capacity
    measured byte capacity
    VID/PID when available
    serial when available
    controller / USB port
    logical and physical sector size
    operating-system build
    DEADFLASH commit SHA
    image SHA-256
    temperature notes
    every successful and failed run

REQUIRED PHASES PER DEVICE
--------------------------

    1. Full verified baseline write.
    2. Offline proof immediately after write.
    3. Offline proof after safe removal and reconnect.
    4. Offline proof after host reboot or full power-cycle.
    5. Unplug during early write.
    6. Unplug during late write.
    7. Unplug during readback verification.
    8. Repeat baseline at least five times for throughput statistics.

RUFUS COMPARISON
----------------

A Rufus comparison is valid only when both tools use:

    same physical device
    same source image and SHA-256
    same USB controller and port
    same device conditioning
    same write/flush boundary
    same verification class
    randomized AB/BA run order
    at least five valid runs per tool

Do not compare DEADFLASH full proof mode against a Rufus write-only run under a
single speed number. Preserve the full Rufus log, settings, timestamps, and all
failed runs. See docs/BENCHMARK_PROTOCOL.md.

NO MAGIC
--------

The harness cannot physically unplug or power-cycle hardware. The operator must
perform those actions. A script record proves what the process observed; it does
not prove an unplug occurred unless the device actually disappeared and the
writer returned a non-success state.
