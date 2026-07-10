#!/usr/bin/env python3
"""Measure DEADFLASH plan sealing, proof creation, proof verification, and fault localization."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from typing import Iterable


def parse_size(text: str) -> int:
    units = {
        "": 1,
        "b": 1,
        "kib": 1024,
        "mib": 1024**2,
        "gib": 1024**3,
    }
    value = text.strip().lower()
    for suffix in ("gib", "mib", "kib", "b"):
        if value.endswith(suffix):
            number = value[: -len(suffix)]
            return int(number, 10) * units[suffix]
    return int(value, 10)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_timed(argv: list[str], expected_codes: Iterable[int] = (0,)) -> tuple[float, str, int]:
    started = time.perf_counter_ns()
    process = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, check=False)
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000.0
    if process.returncode not in set(expected_codes):
        command = subprocess.list2cmdline(argv)
        raise RuntimeError(
            f"command failed with {process.returncode}: {command}\n{process.stdout}"
        )
    return elapsed_ms, process.stdout, process.returncode


def fields(output: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for line in output.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        parsed[key.strip()] = value.strip()
    return parsed


def stats(values: list[float]) -> dict[str, float]:
    return {
        "minimum": min(values),
        "median": statistics.median(values),
        "maximum": max(values),
        "mean": statistics.fmean(values),
        "sample_stddev": statistics.stdev(values) if len(values) > 1 else 0.0,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark DEADFLASH proof phases without hiding correctness cost."
    )
    parser.add_argument("proof_exe", type=Path)
    parser.add_argument("image", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--chunk", default="4MiB")
    parser.add_argument("--buffer", default="32MiB")
    parser.add_argument("--out", type=Path, default=Path("deadflash-proof-benchmark"))
    parser.add_argument(
        "--fault-offset",
        type=int,
        help="For a regular-file target, copy it and flip this exact byte.",
    )
    args = parser.parse_args()

    if args.runs < 1:
        parser.error("--runs must be at least one")
    chunk_bytes = parse_size(args.chunk)
    buffer_bytes = parse_size(args.buffer)
    if chunk_bytes < 4096:
        parser.error("--chunk must be at least 4 KiB")
    if not args.proof_exe.is_file():
        parser.error(f"proof executable not found: {args.proof_exe}")
    if not args.image.is_file():
        parser.error(f"image not found: {args.image}")

    args.out.mkdir(parents=True, exist_ok=True)
    image_size = args.image.stat().st_size
    image_hash = sha256_file(args.image)
    records: list[dict[str, object]] = []

    for run_index in range(1, args.runs + 1):
        manifest = args.out / f"run-{run_index:02d}.dfp"
        if manifest.exists():
            manifest.unlink()

        seal_ms, seal_output, _ = run_timed([
            str(args.proof_exe), "seal", str(args.image), str(args.target),
            "--verify", "full", "--buffer", args.buffer,
        ])
        seal_fields = fields(seal_output)
        plan_seal = seal_fields.get("PLAN_SEAL", "")
        if len(plan_seal) != 64:
            raise RuntimeError(f"invalid plan seal in run {run_index}\n{seal_output}")

        create_ms, create_output, _ = run_timed([
            str(args.proof_exe), "manifest", str(args.image), str(manifest),
            "--chunk", args.chunk,
        ])
        create_fields = fields(create_output)
        if create_fields.get("STATE") != "proof_created":
            raise RuntimeError(f"proof creation failed in run {run_index}\n{create_output}")

        verify_ms, verify_output, _ = run_timed([
            str(args.proof_exe), "verify", str(manifest),
            str(args.image), str(args.target),
        ])
        verify_fields = fields(verify_output)
        if verify_fields.get("STATE") != "success_proven":
            raise RuntimeError(f"proof verification failed in run {run_index}\n{verify_output}")

        records.append({
            "run": run_index,
            "plan_seal_ms": round(seal_ms, 3),
            "proof_create_ms": round(create_ms, 3),
            "proof_verify_ms": round(verify_ms, 3),
            "source_size": image_size,
            "chunk_bytes": chunk_bytes,
            "chunk_count": int(create_fields["CHUNK_COUNT"]),
            "source_sha256": create_fields["SOURCE_SHA256"],
            "merkle_root": create_fields["MERKLE_ROOT"],
            "plan_seal": plan_seal,
            "state": verify_fields["STATE"],
        })

    fault_result: dict[str, object] | None = None
    if args.fault_offset is not None:
        if not args.target.is_file():
            parser.error("--fault-offset requires a regular-file target")
        if args.fault_offset < 0 or args.fault_offset >= image_size:
            parser.error("--fault-offset must be inside the source image region")
        fault_target = args.out / "fault-target.bin"
        shutil.copyfile(args.target, fault_target)
        with fault_target.open("r+b") as stream:
            stream.seek(args.fault_offset)
            original = stream.read(1)
            if len(original) != 1:
                raise RuntimeError("could not read fault byte")
            stream.seek(args.fault_offset)
            stream.write(bytes((original[0] ^ 0xFF,)))
            stream.flush()
            os.fsync(stream.fileno())

        manifest = args.out / "run-01.dfp"
        locate_ms, locate_output, return_code = run_timed([
            str(args.proof_exe), "verify", str(manifest),
            str(args.image), str(fault_target),
        ], expected_codes=range(1, 256))
        locate_fields = fields(locate_output)
        reported = int(locate_fields.get("FIRST_BAD_OFFSET", "-1"))
        if locate_fields.get("STATE") != "target_mismatch" or reported != args.fault_offset:
            raise RuntimeError(
                "fault localization did not report the injected byte\n" + locate_output
            )
        fault_result = {
            "injected_bad_offset": args.fault_offset,
            "reported_bad_offset": reported,
            "exact_offset": True,
            "localization_ms": round(locate_ms, 3),
            "return_code": return_code,
            "state": locate_fields["STATE"],
        }

    csv_path = args.out / "proof-runs.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0].keys()))
        writer.writeheader()
        writer.writerows(records)

    summary = {
        "schema": "deadflash.proof-benchmark.summary.v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "proof_executable": str(args.proof_exe),
        "image": str(args.image),
        "image_size": image_size,
        "image_sha256": image_hash,
        "target": str(args.target),
        "runs": args.runs,
        "chunk_bytes": chunk_bytes,
        "buffer_bytes": buffer_bytes,
        "plan_seal_ms": stats([float(r["plan_seal_ms"]) for r in records]),
        "proof_create_ms": stats([float(r["proof_create_ms"]) for r in records]),
        "proof_verify_ms": stats([float(r["proof_verify_ms"]) for r in records]),
        "states": sorted({str(r["state"]) for r in records}),
        "fault_localization": fault_result,
        "scope": "file-backed proof benchmark; not a physical USB or Rufus comparison",
    }
    summary_path = args.out / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
