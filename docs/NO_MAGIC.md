DEADFLASH NO-MAGIC CONTRACT
===========================

DEADFLASH DOES NOT CLAIM
------------------------

    atomic writes to USB media
    guaranteed recovery after power loss
    proof authenticity without an external signature
    exact mismatch location without the original source bytes
    hardware identity from a path-only token
    performance superiority without equal-policy benchmarks
    production qualification without physical-device tests

DEADFLASH DOES CLAIM
--------------------

    every accepted plan field is serialized and hashed
    changed plans reject stale seals
    short I/O is a failure
    source mutation cannot become verified or proven success
    flush is a measured phase
    readback strength is named explicitly
    proof manifests are structurally and cryptographically checked
    a source/target mismatch is localized to the first byte when possible
    partial-media states remain partial-media states

TERMINOLOGY
-----------

    verified
        The selected post-flush readback policy succeeded.

    proven
        The proof manifest, original source, and target agree chunk by chunk.

    attested
        The live source, target, and policy reproduce the supplied plan seal.

    signed
        Not implemented. A SHA-256 seal or Merkle root is not a signature.

    transactional
        Not used. Physical USB writes are not atomic transactions.
