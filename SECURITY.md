DEADFLASH SECURITY
==================

SUPPORTED VERSION
-----------------

    1.0.x

REPORTING
---------

Do not publish a destructive-storage vulnerability before a fix is available.
Open a private GitHub security advisory in the DEADFLASH repository and include:

    version and commit
    operating system
    exact target path and geometry
    command line with secrets removed
    evidence report
    expected behavior
    observed behavior
    reproducible test image when safe to share

HIGH-SEVERITY CONDITIONS
------------------------

    wrong-device write
    system-disk guard bypass
    stale identity accepted
    false success after corruption
    verification mismatch reported as success
    source hash/report mismatch
    out-of-bounds target write
