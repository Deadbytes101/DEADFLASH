DEADFLASH / RUFUS COMPARISON RECORD
===================================

PURPOSE
-------

Store raw runs in a form that can be rejected when hardware, image, operating
system, conditioning, or correctness policy differs.

The comparator does not automate Rufus. The operator must preserve the Rufus
log and record the observed completion boundary honestly.

RUN SCHEMA
----------

One JSON object per line:

    deadflash.tool-comparison.run.v1

Required fields:

    schema
    run_id
    tool
    tool_version
    device_id
    image_sha256
    image_bytes
    port_id
    controller
    os_build
    correctness_class
    conditioning
    order_index
    success
    total_ms
    evidence_path

Example:

    {"schema":"deadflash.tool-comparison.run.v1","run_id":"deadflash-01","tool":"DEADFLASH","tool_version":"1.0.0-candidate+COMMIT","device_id":"TOKEN-V2-AND-MODEL","image_sha256":"64_LOWERCASE_HEX","image_bytes":8589934592,"port_id":"rear-xhci-port-03","controller":"PCI-VEN_XXXX-DEV_YYYY","os_build":"Windows 11 BUILD","correctness_class":"D_FULL_VERIFY","conditioning":"full-overwrite-idle-10m","order_index":1,"success":true,"total_ms":123456.789,"evidence_path":"bench/hardware/DEVICE-A/deadflash/run-01.json"}

CORRECTNESS CLASSES
-------------------

    A_WRITE_ONLY
        The timed boundary excludes an explicit final cache flush and readback.

    B_WRITE_FLUSH
        The timed boundary includes successful final cache flush.

    C_SAMPLE_VERIFY
        The timed boundary includes flush and the same deterministic sample
        readback policy for both tools.

    D_FULL_VERIFY
        The timed boundary includes flush and full byte-for-byte or equivalent
        full-image readback verification for both tools.

Do not label a Rufus run as class D unless an external full readback is included
inside the same measured boundary. A DEADFLASH class D run cannot be compared
to a Rufus write-only completion dialog.

CONTEXT FIELDS
--------------

The comparator requires exact equality for:

    device_id
    image_sha256
    image_bytes
    port_id
    controller
    os_build
    correctness_class
    conditioning

It rejects:

    duplicate or missing run IDs
    incomplete order indexes
    unbalanced allocation
    mixed tool versions
    fewer than five successful runs per tool by default
    invalid hashes, durations, or correctness classes
    any comparison-context mismatch

FAILED RUNS
-----------

Failed records stay in the JSONL file with:

    success = false

They count toward failure rate and appear in the summary. Timing statistics use
successful runs only, while the minimum-success gate prevents a tool with too
few valid runs from receiving a comparison result.

COMPARATOR
----------

    python scripts/compare-benchmarks.py bench/hardware/DEVICE-A/runs.jsonl \
        --output-json bench/hardware/DEVICE-A/comparison.json \
        --output-text bench/hardware/DEVICE-A/comparison.txt \
        --require-evidence-files

Output includes:

    minimum / median / maximum
    arithmetic mean
    sample standard deviation
    failure rate
    median effective MiB/s
    observed median throughput delta
    deterministic bootstrap 95% range
    scoped result language

CLAIM RULE
----------

Valid:

    DEADFLASH had 7.8% higher median effective throughput on DEVICE-A under
    D_FULL_VERIFY, with the published image, port, controller, conditioning,
    raw runs, and bootstrap range.

Invalid:

    DEADFLASH is faster than Rufus.

A speed result is not a reliability result. Reliability claims require the
physical fault records described in `docs/USB_QUALIFICATION.md`.
