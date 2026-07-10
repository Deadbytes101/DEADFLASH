#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def run(argv: list[str], expected: int = 0) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(argv, text=True, capture_output=True, check=False)
    if completed.returncode != expected:
        print(completed.stdout, file=sys.stderr)
        print(completed.stderr, file=sys.stderr)
        raise RuntimeError(f"unexpected exit {completed.returncode}: {argv!r}")
    return completed


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    tool = root / "scripts" / "make-qualification-image.py"
    with tempfile.TemporaryDirectory(prefix="deadflash-qualification-image-") as temporary:
        tmp = pathlib.Path(temporary)
        image = tmp / "qualification.img"
        manifest = tmp / "qualification.json"
        first = run([
            sys.executable, str(tool), "create", str(image),
            "--manifest", str(manifest), "--size", "5MiB", "--chunk", "1MiB",
            "--seed", "fixture-seed",
        ])
        created = json.loads(first.stdout)
        if created.get("state") != "created":
            return 1
        verified = json.loads(run([
            sys.executable, str(tool), "verify", str(image), "--manifest", str(manifest)
        ]).stdout)
        if verified.get("state") != "success_verified":
            return 2

        bad_offset = 2 * 1024 * 1024 + 77
        with image.open("r+b", buffering=0) as stream:
            stream.seek(bad_offset)
            original = stream.read(1)
            if len(original) != 1:
                return 3
            stream.seek(bad_offset)
            stream.write(bytes([original[0] ^ 0x5A]))
        corrupted = json.loads(run([
            sys.executable, str(tool), "verify", str(image), "--manifest", str(manifest)
        ], expected=1).stdout)
        if corrupted.get("state") != "content_mismatch":
            return 4
        if corrupted.get("first_bad_offset") != bad_offset:
            return 5
    print("QUALIFICATION IMAGE CONTRACT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
