DEADFLASH WINDOWS / MSVC QUALIFICATION
======================================

VERSION: 1.0.0 CANDIDATE

PURPOSE
-------

Produce a machine-readable Windows compiler and file-backed E2E record before
any physical-device qualification begins.

This is a safe qualification stage. It writes only regular files created by the
E2E harness. Administrator privileges are not required for this stage.

REQUIREMENTS
------------

    Windows 10 or Windows 11 x64
    Visual Studio 2022 C++ x64 tools
    Windows SDK
    CMake
    Python 3
    PowerShell 5.1 or PowerShell 7

The script accepts an existing Developer PowerShell environment. If `cl.exe` is
not already in PATH, it uses `vswhere.exe` and imports `vcvars64.bat` into the
current process.

RUN
---

From the repository root:

    powershell -ExecutionPolicy Bypass -File scripts\qualify-msvc.ps1

Custom output paths:

    powershell -ExecutionPolicy Bypass -File scripts\qualify-msvc.ps1 ^
        -BuildDir C:\deadflash-build ^
        -EvidenceDir C:\deadflash-evidence

WHAT IT EXECUTES
----------------

    cl.exe /Bv
    cmake configure with x64 Visual Studio generator
    cmake Release build with warnings-as-errors
    ctest Release suite
    scripts/e2e-proof.py against the MSVC Release binaries

PASS CONTRACT
-------------

    compiler environment discovered
    Release build exits 0
    all seven tests pass
    write_state       = success_verified
    proof_state       = success_proven
    corruption_state  = target_mismatch
    injected offset   = reported offset

OUTPUT
------

The script writes two timestamped JSON files under `bench\results` by default:

    msvc-qualification-YYYYMMDDTHHMMSSZ.json
    msvc-e2e-YYYYMMDDTHHMMSSZ.json

The qualification record contains:

    Windows description and architecture
    PowerShell version
    Visual Studio environment source
    repository commit when Git is available
    complete command arguments
    exit codes
    elapsed time
    stdout and stderr
    test summary
    parsed E2E evidence
    failure type, message, and script stack when a stage fails

FAILURE POLICY
--------------

A failed build or test still writes a qualification JSON record. Do not delete
or replace failed evidence. Fix the defect and retain both failed and passing
records so the release history remains auditable.

BOUNDARY
--------

A passing MSVC/file-backed record does not qualify `\\.\PhysicalDriveN`, volume
locking, storage descriptor queries, unplug behavior, power-cycle behavior, or
USB throughput. Continue with `docs/USB_QUALIFICATION.md` on sacrificial media.

NO MAGIC
--------

`clang --driver-mode=cl` and the Win32 surface stubs detect many conditional
source defects early, but only this Windows SDK/MSVC execution can close the
MSVC software gate.
