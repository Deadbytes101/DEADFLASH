DEADFLASH QUALIFICATION IMAGE
=============================

PURPOSE
-------

Create the exact same source bytes for physical USB qualification and
DEADFLASH/Rufus comparison runs. The image is generated from a recorded recipe,
not copied from an undocumented temporary file.

ALGORITHM
---------

    deadflash.qualification-image.v1
    shake256-offset-chunk-v1

Each chunk is generated independently from:

    domain separator
    UTF-8 seed length and seed bytes
    absolute chunk offset
    exact chunk length

The manifest stores:

    recipe schema and algorithm
    SHA-256 recipe ID
    seed as hexadecimal bytes
    image size
    generation chunk size
    full image SHA-256
    image filename

The recipe ID is an integrity identifier, not a signature.

CREATE
------

Example 8 GiB image:

    python scripts/make-qualification-image.py create qualification-8g.img \
        --manifest qualification-8g.json \
        --size 8GiB \
        --chunk 4MiB \
        --seed DEADFLASH-V1-PHYSICAL-QUALIFICATION

The writer creates a temporary file, fsyncs it, atomically replaces the final
image, then writes and fsyncs the manifest. Existing outputs are rejected unless
`--force` is supplied explicitly.

VERIFY
------

    python scripts/make-qualification-image.py verify qualification-8g.img \
        --manifest qualification-8g.json

A valid image returns:

    success_verified

A corrupted image returns nonzero and reports:

    content_mismatch
    first_bad_offset
    expected_byte
    actual_byte
    bytes_verified

A size change returns `size_mismatch`. A manifest whose canonical recipe no
longer matches its recipe ID is rejected before image verification.

BENCHMARK CONTRACT
------------------

For one comparison dataset, both tools must use the same:

    manifest file
    recipe ID
    image SHA-256
    image byte length

Regenerating from the same recipe is acceptable only after `verify` returns
`success_verified` and the resulting SHA-256 matches the recorded comparison
context.

Do not change seed, size, or chunk size midway through a comparison. Do not use
a zero-filled or sparse source for a general throughput claim unless that exact
content class is the declared subject of the claim.

NO MAGIC
--------

Deterministic generation proves that two source files can be reproduced from
the same documented recipe. It does not prove that a USB controller wrote those
bytes correctly; that still requires DEADFLASH readback/proof and the physical
qualification records.
