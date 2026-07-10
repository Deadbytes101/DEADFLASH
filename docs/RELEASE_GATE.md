DEADFLASH 1.0.0 RELEASE GATE
============================

SOFTWARE GATE
-------------

    [ ] GCC warnings-as-errors build passes
    [ ] Clang warnings-as-errors build passes
    [ ] MSVC warnings-as-errors build passes
    [ ] ASan passes
    [ ] UBSan passes
    [ ] SHA-256 vectors pass
    [ ] Pipeline full/sample verification passes
    [ ] FAT32 structural validation passes
    [ ] Target identity-change test passes
    [ ] Plan-seal mutation test passes
    [ ] Merkle manifest validation passes
    [ ] Exact bad-byte offset test passes

PHYSICAL DEVICE GATE
--------------------

    [ ] USB 2.0 flash drive write/flush/full verify
    [ ] USB 3.x flash drive write/flush/full verify
    [ ] USB-SATA bridge write/flush/full verify
    [ ] Unplug during write produces partial-media failure
    [ ] Unplug during verify produces verification failure
    [ ] Power-cycle then offline proof verify passes
    [ ] Injected corruption reports exact byte offset
    [ ] Running system disk is rejected
    [ ] Mounted target is rejected or locked/dismounted as designed

EVIDENCE GATE
-------------

    [ ] Raw JSON evidence retained
    [ ] Raw benchmark CSV retained
    [ ] Failed runs retained
    [ ] Commit IDs and tool versions retained
    [ ] Rufus comparison uses equal correctness class
    [ ] No broad superiority language appears without measured scope

TAG POLICY
----------

Do not create `v1.0.0` until every required box above has evidence attached to
a release issue or pull request.
