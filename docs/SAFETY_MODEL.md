DEADFLASH SAFETY MODEL
======================

THREAT
------

The primary threat is not an attacker. It is an authorized program writing the
wrong storage device because of stale identity, drive-letter churn, automount,
ambiguous UI state, or optimistic error handling.

TARGET IDENTITY
---------------

The confirmation token is derived from:

    path
    target kind
    size in bytes
    logical sector size
    physical sector size
    read-only state
    system-disk classification

The token is not a cryptographic device certificate. Its purpose is stale-plan
and accidental-target detection. The full identity is reinspected before the
write handle is opened.

FAIL-CLOSED RULES
-----------------

    - Unknown target type: reject.
    - Physical target without allow flag: reject.
    - Missing or stale confirmation token: reject.
    - Read-only target: reject.
    - Source larger than target: reject.
    - Running system disk: reject.
    - Mounted POSIX target: reject unless explicitly overridden.
    - Windows volume lock failure: reject.
    - Short source read: fail.
    - Short target write after retries: fail_partial_media.
    - Flush failure: fail_partial_media.
    - Readback mismatch: verification_failed.

OVERRIDES
---------

`--force-system-disk` exists for controlled laboratory work. It should never be
exposed as a normal GUI checkbox. A production frontend should hide it behind a
developer build or compile-time flag.

`--force-mounted` is a POSIX laboratory override. Writing a mounted filesystem
can corrupt both the target and the running kernel's cached view.

CANCELLATION
------------

Version 1.0.0 does not implement asynchronous cancellation. Killing the process
can leave partially written media. The media must then be treated as invalid
until rewritten and verified.

TRUST BOUNDARY
--------------

A USB controller can lie about flush completion, capacity, or readback. Full
verification detects many failures but cannot prove persistence across power
loss. Hardware qualification should include power-cycle verification.
