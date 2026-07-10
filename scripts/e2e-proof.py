#!/usr/bin/env python3
"""Cross-platform DEADFLASH end-to-end proof qualification.

This harness only uses regular files. It is safe for CI and validates the same
frontend/control path on Linux and Windows without touching a physical device.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass


@dataclass
class CommandResult:
    argv: list[str]
    returncode: int
    stdout: str
    stderr: str
    elapsed_ms: float


def run(argv: list[str], expected: int | set[int] = 0) -> CommandResult:
    start = time.perf_counter()
    completed = subprocess.run(argv, text=True, capture_output=True, check=False)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    accepted = {expected} if isinstance(expected, int) else expected
    result = CommandResult(argv, completed.returncode, completed.stdout, completed.stderr, elapsed_ms)
    if completed.returncode not in accepted:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {argv!r}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return result


def parse_fields(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        normalized = key.strip().lower().replace(" ", "_")
        fields[normalized] = value.strip()
    return fields


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def create_source(path: pathlib.Path, size: int) -> None:
    state = 0xD34DF1A5C0FFEE01
    remaining = size
    with path.open("wb") as stream:
        while remaining:
            count = min(1024 * 1024, remaining)
            block = bytearray(count)
            for index in range(count):
                state ^= (state << 13) & 0xFFFFFFFFFFFFFFFF
                state ^= state >> 7
                state ^= (state << 17) & 0xFFFFFFFFFFFFFFFF
                block[index] = state & 0xFF
            stream.write(block)
            remaining -= count


def corrupt_byte(path: pathlib.Path, offset: int) -> tuple[int, int]:
    with path.open("r+b", buffering=0) as stream:
        stream.seek(offset)
        original_raw = stream.read(1)
        if len(original_raw) != 1:
            raise RuntimeError(f"cannot read corruption byte at offset {offset}")
        original = original_raw[0]
        replacement = original ^ 0xA5
        stream.seek(offset)
        stream.write(bytes([replacement]))
        stream.flush()
        os.fsync(stream.fileno())
    return original, replacement


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--deadflash", required=True, type=pathlib.Path)
    parser.add_argument("--proof", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    parser.add_argument("--summary", type=pathlib.Path)
    parser.add_argument("--size", type=int, default=6 * 1024 * 1024 + 123)
    parser.add_argument("--chunk", default="1MiB")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    deadflash = args.deadflash.resolve()
    proof_exe = args.proof.resolve()
    work_dir = args.work_dir.resolve()
    summary_path = (args.summary or (work_dir / "summary.json")).resolve()

    if args.size < 4096:
        parser.error("--size must be at least 4096 bytes")
    if not deadflash.is_file() or not proof_exe.is_file():
        parser.error("both executables must exist")

    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)
    summary_path.parent.mkdir(parents=True, exist_ok=True)

    source = work_dir / "source.img"
    target = work_dir / "target.img"
    manifest = work_dir / "source.dfp"
    create_source(source, args.size)
    target.write_bytes(b"\0" * args.size)

    source_hash = sha256_file(source)
    commands: list[dict[str, object]] = []

    version = run([str(deadflash), "version"])
    proof_version = run([str(proof_exe), "version"])

    seal_result = run(
        [
            str(proof_exe),
            "seal",
            str(source),
            str(target),
            "--verify",
            "full",
            "--buffer",
            "1MiB",
        ]
    )
    seal_fields = parse_fields(seal_result.stdout)
    plan_seal = seal_fields.get("plan_seal", "")
    if len(plan_seal) != 64:
        raise RuntimeError(f"invalid PLAN_SEAL output: {plan_seal!r}")
    if seal_fields.get("source_sha256") != source_hash:
        raise RuntimeError("seal source hash does not match independently computed SHA-256")

    write_result = run(
        [
            str(proof_exe),
            "write",
            str(source),
            str(target),
            "--seal",
            plan_seal,
            "--verify",
            "full",
            "--buffer",
            "1MiB",
            "--proof",
            str(manifest),
            "--chunk",
            args.chunk,
        ]
    )
    write_fields = parse_fields(write_result.stdout)
    if write_fields.get("state") != "success_verified":
        raise RuntimeError(f"write did not reach success_verified: {write_fields!r}")
    if write_fields.get("proof_state") != "success_proven":
        raise RuntimeError(f"write proof did not reach success_proven: {write_fields!r}")
    if sha256_file(target) != source_hash:
        raise RuntimeError("target SHA-256 differs after verified write")

    verify_result = run([str(proof_exe), "verify", str(manifest), str(source), str(target)])
    verify_fields = parse_fields(verify_result.stdout)
    if verify_fields.get("state") != "success_proven":
        raise RuntimeError(f"offline proof did not reach success_proven: {verify_fields!r}")

    bad_offset = args.size // 2 + 17
    original, replacement = corrupt_byte(target, bad_offset)
    bad_result = run(
        [str(proof_exe), "verify", str(manifest), str(source), str(target)],
        expected={1},
    )
    bad_fields = parse_fields(bad_result.stdout)
    reported_text = bad_fields.get("first_bad_offset")
    if bad_fields.get("state") != "target_mismatch":
        raise RuntimeError(f"corruption was not classified as target_mismatch: {bad_fields!r}")
    if reported_text is None or int(reported_text) != bad_offset:
        raise RuntimeError(
            f"wrong corruption offset: injected={bad_offset} reported={reported_text!r}"
        )

    for item in (version, proof_version, seal_result, write_result, verify_result, bad_result):
        commands.append(
            {
                "argv": item.argv,
                "returncode": item.returncode,
                "elapsed_ms": round(item.elapsed_ms, 3),
                "stdout": item.stdout,
                "stderr": item.stderr,
            }
        )

    summary = {
        "schema": "deadflash.e2e-proof.v1",
        "platform": sys.platform,
        "python": sys.version,
        "source_size": args.size,
        "source_sha256": source_hash,
        "plan_seal": plan_seal,
        "chunk": args.chunk,
        "write_state": write_fields.get("state"),
        "proof_state": verify_fields.get("state"),
        "corruption_state": bad_fields.get("state"),
        "injected_bad_offset": bad_offset,
        "reported_bad_offset": int(reported_text),
        "original_byte": original,
        "replacement_byte": replacement,
        "commands": commands,
    }
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))

    if not args.keep:
        for path in (source, target, manifest):
            path.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # CI must print the exact failure cause.
        print(f"E2E FAILURE: {exc}", file=sys.stderr)
        raise SystemExit(1)
