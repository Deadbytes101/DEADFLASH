#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def records() -> list[dict[str, object]]:
    output: list[dict[str, object]] = []
    order = 1
    for run in range(5):
        for tool, total_ms in (("DEADFLASH", 100000 + run * 500), ("Rufus", 105000 + run * 500)):
            output.append(
                {
                    "schema": "deadflash.tool-comparison.run.v1",
                    "run_id": f"{tool}-{run + 1}",
                    "tool": tool,
                    "tool_version": "fixture-1",
                    "device_id": "fixture-device-token-v2",
                    "image_sha256": "a" * 64,
                    "image_bytes": 8 * 1024 * 1024 * 1024,
                    "port_id": "fixture-port",
                    "controller": "fixture-controller",
                    "os_build": "fixture-os",
                    "correctness_class": "D_FULL_VERIFY",
                    "conditioning": "fixture-conditioning",
                    "order_index": order,
                    "success": True,
                    "total_ms": float(total_ms),
                    "evidence_path": f"fixture/{tool}-{run + 1}.json",
                }
            )
            order += 1
    return output


def write_jsonl(path: pathlib.Path, rows: list[dict[str, object]]) -> None:
    path.write_text("\n".join(json.dumps(row, separators=(",", ":")) for row in rows) + "\n", encoding="utf-8")


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    comparator = root / "scripts" / "compare-benchmarks.py"
    with tempfile.TemporaryDirectory(prefix="deadflash-comparison-") as temporary:
        tmp = pathlib.Path(temporary)
        valid = tmp / "valid.jsonl"
        summary = tmp / "summary.json"
        report = tmp / "summary.txt"
        rows = records()
        write_jsonl(valid, rows)
        passed = subprocess.run(
            [sys.executable, str(comparator), str(valid), "--output-json", str(summary), "--output-text", str(report)],
            text=True,
            capture_output=True,
            check=False,
        )
        if passed.returncode != 0:
            print(passed.stdout, file=sys.stderr)
            print(passed.stderr, file=sys.stderr)
            return 1
        parsed = json.loads(summary.read_text(encoding="utf-8"))
        if parsed.get("schema") != "deadflash.tool-comparison.summary.v1":
            return 2
        if parsed["tools"]["DEADFLASH"]["runs_successful"] != 5:
            return 3

        rows[-1]["port_id"] = "wrong-port"
        invalid = tmp / "invalid.jsonl"
        write_jsonl(invalid, rows)
        rejected = subprocess.run(
            [sys.executable, str(comparator), str(invalid), "--output-json", str(tmp / "bad.json"), "--output-text", str(tmp / "bad.txt")],
            text=True,
            capture_output=True,
            check=False,
        )
        if rejected.returncode == 0:
            return 4
        if "comparison context mismatch" not in rejected.stderr:
            print(rejected.stderr, file=sys.stderr)
            return 5
    print("BENCHMARK COMPARATOR CONTRACT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
