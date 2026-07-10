#!/usr/bin/env python3
"""Create and verify deterministic DEADFLASH qualification images."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import sys
import tempfile
from typing import BinaryIO

SCHEMA = "deadflash.qualification-image.v1"
ALGORITHM = "shake256-offset-chunk-v1"
DOMAIN = b"deadflash.qualification-image.v1\0"
DEFAULT_SEED = "DEADFLASH-V1-PHYSICAL-QUALIFICATION"
DEFAULT_CHUNK = 4 * 1024 * 1024


def parse_size(text: str) -> int:
    match = re.fullmatch(r"\s*([0-9]+)\s*([KMGT]?)(?:i?[bB])?\s*", text, re.IGNORECASE)
    if match is None:
        raise argparse.ArgumentTypeError(f"invalid size: {text!r}")
    value = int(match.group(1))
    unit = match.group(2).upper()
    multiplier = {"": 1, "K": 1024, "M": 1024**2, "G": 1024**3, "T": 1024**4}[unit]
    result = value * multiplier
    if result <= 0:
        raise argparse.ArgumentTypeError("size must be greater than zero")
    return result


def recipe_bytes(seed: bytes, offset: int, length: int) -> bytes:
    recipe = DOMAIN + len(seed).to_bytes(4, "little") + seed
    recipe += offset.to_bytes(8, "little") + length.to_bytes(8, "little")
    return hashlib.shake_256(recipe).digest(length)


def canonical_recipe(seed_hex: str, size: int, chunk_bytes: int) -> bytes:
    record = {
        "algorithm": ALGORITHM,
        "chunk_bytes": chunk_bytes,
        "schema": SCHEMA,
        "seed_hex": seed_hex,
        "size_bytes": size,
    }
    return json.dumps(record, sort_keys=True, separators=(",", ":")).encode("utf-8")


def fsync_directory(path: pathlib.Path) -> None:
    if os.name == "nt":
        return
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def create_image(
    output: pathlib.Path,
    manifest_path: pathlib.Path,
    size: int,
    chunk_bytes: int,
    seed: bytes,
    force: bool,
) -> dict[str, object]:
    if size < 1024 * 1024:
        raise ValueError("qualification image must be at least 1 MiB")
    if chunk_bytes < 4096 or chunk_bytes > 1024 * 1024 * 1024:
        raise ValueError("chunk size must be between 4 KiB and 1 GiB")
    if output.exists() and not force:
        raise ValueError(f"output already exists: {output}")
    if manifest_path.exists() and not force:
        raise ValueError(f"manifest already exists: {manifest_path}")

    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=output.name + ".", suffix=".tmp", dir=output.parent
    )
    temporary = pathlib.Path(temporary_name)
    digest = hashlib.sha256()
    offset = 0
    try:
        with os.fdopen(descriptor, "wb", buffering=0) as stream:
            while offset < size:
                count = min(chunk_bytes, size - offset)
                block = recipe_bytes(seed, offset, count)
                written = stream.write(block)
                if written != count:
                    raise OSError(f"short qualification-image write: {written}/{count}")
                digest.update(block)
                offset += count
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
        fsync_directory(output.parent)
    finally:
        temporary.unlink(missing_ok=True)

    seed_hex = seed.hex()
    recipe_id = hashlib.sha256(canonical_recipe(seed_hex, size, chunk_bytes)).hexdigest()
    record: dict[str, object] = {
        "schema": SCHEMA,
        "algorithm": ALGORITHM,
        "recipe_id": recipe_id,
        "seed_hex": seed_hex,
        "size_bytes": size,
        "chunk_bytes": chunk_bytes,
        "image_sha256": digest.hexdigest(),
        "image_name": output.name,
    }
    temporary_manifest = manifest_path.with_name(manifest_path.name + ".tmp")
    with temporary_manifest.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(record, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary_manifest, manifest_path)
    fsync_directory(manifest_path.parent)
    return record


def first_difference(expected: bytes, actual: bytes) -> int | None:
    limit = min(len(expected), len(actual))
    for index in range(limit):
        if expected[index] != actual[index]:
            return index
    if len(expected) != len(actual):
        return limit
    return None


def read_exact(stream: BinaryIO, count: int) -> bytes:
    data = stream.read(count)
    if len(data) != count:
        raise EOFError(f"short image read: {len(data)}/{count}")
    return data


def verify_image(image: pathlib.Path, manifest_path: pathlib.Path) -> dict[str, object]:
    record = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(record, dict) or record.get("schema") != SCHEMA:
        raise ValueError("unsupported qualification-image manifest")
    if record.get("algorithm") != ALGORITHM:
        raise ValueError("unsupported qualification-image algorithm")

    seed_hex = str(record.get("seed_hex", ""))
    try:
        seed = bytes.fromhex(seed_hex)
    except ValueError as exc:
        raise ValueError("manifest seed_hex is invalid") from exc
    size = int(record.get("size_bytes", 0))
    chunk_bytes = int(record.get("chunk_bytes", 0))
    expected_hash = str(record.get("image_sha256", "")).lower()
    expected_recipe = hashlib.sha256(canonical_recipe(seed_hex, size, chunk_bytes)).hexdigest()
    if record.get("recipe_id") != expected_recipe:
        raise ValueError("manifest recipe_id does not match its canonical recipe")
    if len(expected_hash) != 64 or any(ch not in "0123456789abcdef" for ch in expected_hash):
        raise ValueError("manifest image_sha256 is invalid")
    if image.stat().st_size != size:
        return {
            "state": "size_mismatch",
            "expected_size": size,
            "actual_size": image.stat().st_size,
            "first_bad_offset": min(size, image.stat().st_size),
        }

    digest = hashlib.sha256()
    offset = 0
    with image.open("rb", buffering=0) as stream:
        while offset < size:
            count = min(chunk_bytes, size - offset)
            actual = read_exact(stream, count)
            expected = recipe_bytes(seed, offset, count)
            digest.update(actual)
            mismatch = first_difference(expected, actual)
            if mismatch is not None:
                return {
                    "state": "content_mismatch",
                    "first_bad_offset": offset + mismatch,
                    "expected_byte": expected[mismatch] if mismatch < len(expected) else None,
                    "actual_byte": actual[mismatch] if mismatch < len(actual) else None,
                    "bytes_verified": offset + mismatch,
                }
            offset += count

    actual_hash = digest.hexdigest()
    if actual_hash != expected_hash:
        return {
            "state": "hash_mismatch",
            "expected_sha256": expected_hash,
            "actual_sha256": actual_hash,
            "bytes_verified": size,
        }
    return {
        "state": "success_verified",
        "image_sha256": actual_hash,
        "bytes_verified": size,
        "recipe_id": expected_recipe,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create")
    create.add_argument("output", type=pathlib.Path)
    create.add_argument("--manifest", required=True, type=pathlib.Path)
    create.add_argument("--size", required=True, type=parse_size)
    create.add_argument("--chunk", type=parse_size, default=DEFAULT_CHUNK)
    create.add_argument("--seed", default=DEFAULT_SEED)
    create.add_argument("--force", action="store_true")

    verify = subparsers.add_parser("verify")
    verify.add_argument("image", type=pathlib.Path)
    verify.add_argument("--manifest", required=True, type=pathlib.Path)

    args = parser.parse_args()
    try:
        if args.command == "create":
            result = create_image(
                args.output.resolve(),
                args.manifest.resolve(),
                args.size,
                args.chunk,
                args.seed.encode("utf-8"),
                args.force,
            )
            result["state"] = "created"
            code = 0
        else:
            result = verify_image(args.image.resolve(strict=True), args.manifest.resolve(strict=True))
            code = 0 if result.get("state") == "success_verified" else 1
    except (OSError, ValueError, EOFError, json.JSONDecodeError) as exc:
        print(f"QUALIFICATION IMAGE ERROR: {exc}", file=sys.stderr)
        return 1

    print(json.dumps(result, indent=2, sort_keys=True))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
