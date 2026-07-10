#!/usr/bin/env python3
"""Append one raw tool-comparison run without rewriting prior evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import sys
import tempfile
from typing import Any

SCHEMA = "deadflash.tool-comparison.run.v1"
ALLOWED_CLASSES = ("A_WRITE_ONLY", "B_WRITE_FLUSH", "C_SAMPLE_VERIFY", "D_FULL_VERIFY")
CONTEXT_FIELDS = (
    "device_id",
    "image_sha256",
    "image_bytes",
    "port_id",
    "controller",
    "os_build",
    "correctness_class",
    "conditioning",
)


def image_identity(path: pathlib.Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while True:
            block = stream.read(4 * 1024 * 1024)
            if not block:
                break
            size += len(block)
            digest.update(block)
    return size, digest.hexdigest()


def load_existing(path: pathlib.Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    rows: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        try:
            row = json.loads(stripped)
        except json.JSONDecodeError as exc:
            raise ValueError(f"existing dataset line {line_number} is invalid JSON: {exc}") from exc
        if not isinstance(row, dict) or row.get("schema") != SCHEMA:
            raise ValueError(f"existing dataset line {line_number} has the wrong schema")
        rows.append(row)
    return rows


def atomic_write(path: pathlib.Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            for row in rows:
                stream.write(json.dumps(row, separators=(",", ":"), sort_keys=True))
                stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=pathlib.Path)
    parser.add_argument("--tool", required=True)
    parser.add_argument("--tool-version", required=True)
    parser.add_argument("--image", required=True, type=pathlib.Path)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--port-id", required=True)
    parser.add_argument("--controller", required=True)
    parser.add_argument("--os-build", required=True)
    parser.add_argument("--correctness-class", choices=ALLOWED_CLASSES, required=True)
    parser.add_argument("--conditioning", required=True)
    parser.add_argument("--total-ms", required=True, type=float)
    parser.add_argument("--evidence-path", required=True, type=pathlib.Path)
    parser.add_argument("--run-id")
    status = parser.add_mutually_exclusive_group(required=True)
    status.add_argument("--success", action="store_true")
    status.add_argument("--failed", action="store_true")
    parser.add_argument("--notes", default="")
    parser.add_argument("--allow-missing-evidence", action="store_true")
    args = parser.parse_args()

    try:
        image = args.image.resolve(strict=True)
        if not image.is_file():
            raise ValueError(f"image is not a regular file: {image}")
        evidence = args.evidence_path.resolve()
        if not args.allow_missing_evidence and not evidence.is_file():
            raise ValueError(f"evidence file does not exist: {evidence}")
        if args.success and args.total_ms <= 0:
            raise ValueError("successful run total-ms must be greater than zero")
        if args.total_ms < 0:
            raise ValueError("total-ms cannot be negative")

        image_bytes, image_sha256 = image_identity(image)
        dataset = args.dataset.resolve()
        rows = load_existing(dataset)
        order_index = len(rows) + 1
        slug = re.sub(r"[^a-z0-9]+", "-", args.tool.lower()).strip("-") or "tool"
        run_id = args.run_id or f"{slug}-{order_index:03d}"
        if any(str(row.get("run_id")) == run_id for row in rows):
            raise ValueError(f"duplicate run_id: {run_id}")

        context = {
            "device_id": args.device_id,
            "image_sha256": image_sha256,
            "image_bytes": image_bytes,
            "port_id": args.port_id,
            "controller": args.controller,
            "os_build": args.os_build,
            "correctness_class": args.correctness_class,
            "conditioning": args.conditioning,
        }
        if rows:
            mismatches = {
                field: (rows[0].get(field), context[field])
                for field in CONTEXT_FIELDS
                if rows[0].get(field) != context[field]
            }
            if mismatches:
                raise ValueError(f"new run does not match existing comparison context: {mismatches}")

        record: dict[str, Any] = {
            "schema": SCHEMA,
            "run_id": run_id,
            "tool": args.tool,
            "tool_version": args.tool_version,
            **context,
            "order_index": order_index,
            "success": bool(args.success),
            "total_ms": args.total_ms,
            "evidence_path": str(evidence),
            "notes": args.notes,
        }
        rows.append(record)
        atomic_write(dataset, rows)
    except (OSError, ValueError) as exc:
        print(f"RUN REJECTED: {exc}", file=sys.stderr)
        return 1

    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
