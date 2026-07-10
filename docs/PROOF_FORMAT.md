DEADFLASH PROOF FORMAT
======================

FORMAT ID
---------

    DEADFLASH-PROOF-V1

ENCODING
--------

ASCII text. Integers are unsigned decimal. Hashes are lowercase hexadecimal
when emitted; readers accept upper or lowercase hexadecimal.

RECORD ORDER
------------

    DEADFLASH-PROOF-V1
    source_size N
    chunk_size N
    chunk_count N
    source_sha256 64_HEX
    chunk 0 LENGTH 64_HEX
    chunk 1 LENGTH 64_HEX
    ...
    merkle_root 64_HEX
    END

INVARIANTS
----------

    - source_size is greater than zero.
    - chunk_size is between 4096 and 1073741824 bytes.
    - chunk_count equals ceil(source_size / chunk_size).
    - chunk indexes are contiguous and start at zero.
    - every non-final chunk length equals chunk_size.
    - final chunk length equals the remaining source bytes.
    - chunk_count is limited to 4194304.
    - the calculated Merkle root must equal merkle_root.

MERKLE CONSTRUCTION
-------------------

Leaf nodes are the 32-byte SHA-256 chunk digests stored in the manifest.

Parent nodes are:

    SHA256(0x01 || LEFT_32_BYTES || RIGHT_32_BYTES)

When a level has an odd node count, the final node is duplicated as its right
sibling. A one-leaf tree has the leaf digest as its root.

SECURITY BOUNDARY
-----------------

The format detects accidental or unauthorized manifest modification only when
the expected Merkle root or entire manifest is stored in a trusted external
location. A root embedded in the same writable manifest is not a signature.

The manifest contains hashes, not source bytes. Exact byte localization
therefore requires the original source image and target readback.

PARSER POLICY
-------------

Readers fail closed on unknown ordering, missing records, invalid lengths,
non-contiguous indexes, invalid hexadecimal, allocation overflow, root
mismatch, or missing END marker.
