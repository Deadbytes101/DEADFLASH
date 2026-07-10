DEADFLASH VS RUFUS: VALID COMPARISON SCOPE
==========================================

RUFUS ADVANTAGE TODAY
---------------------

Rufus is a mature Windows boot-media utility with a much broader feature set.
DEADFLASH 1.0.0 candidate does not claim parity in ISO extraction, Windows
installation customization, WIM handling, persistence, Windows To Go, firmware
validation, download services, or filesystem breadth.

DEADFLASH TARGETED ADVANTAGE
----------------------------

DEADFLASH targets a narrower engineering advantage:

    - operation authorization bound to source, target, and policy
    - explicit post-flush verification classes
    - versioned machine-readable evidence
    - per-chunk cryptographic proof
    - exact first-bad-byte localization
    - raw benchmark and fault records retained in the repository

CLAIM FORMAT
------------

A valid comparison names:

    hardware
    image hash
    tool versions and commits
    correctness class
    run count
    statistic
    measured result
    raw evidence location

Example:

    DEADFLASH located 100/100 injected one-byte corruptions at the exact
    absolute offset on device C under Class G. Rufus did not expose an
    equivalent Class G artifact in this test. Raw records: bench/results/...

This example may be published only after those records exist.

FORBIDDEN CLAIMS
----------------

    DEADFLASH destroys Rufus.
    DEADFLASH is always safer.
    DEADFLASH is always faster.
    Merkle means unhackable.
    Plan seal means signed.
    Verified means bootable.

The machine decides. Marketing does not override missing evidence.
