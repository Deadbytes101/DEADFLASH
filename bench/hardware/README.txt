DEADFLASH HARDWARE EVIDENCE
===========================

Store physical-device qualification records here.

DIRECTORY FORM
--------------

    bench/hardware/DEVICE-ID/
        device.txt
        baseline-*.json
        unplug-early-*.json
        unplug-late-*.json
        unplug-verify-*.json
        reconnect-*.json
        power-cycle-*.json
        baseline.dfp
        benchmark-runs.csv
        benchmark-summary.json
        rufus/
            run-*.log
            run-*.txt

RULES
-----

    - Commit failed runs.
    - Do not rename failures as passes.
    - Do not commit private serial numbers unless deliberately approved.
    - Record the exact DEADFLASH commit SHA.
    - Record the image SHA-256.
    - Record device, port, controller, and operating-system build.
    - Keep Rufus and DEADFLASH correctness classes equal.
    - Never place production data on qualification media.

No physical-device pass records exist merely because this directory exists.
Evidence is valid only when produced by an actual sacrificial-device run.
