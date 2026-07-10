#!/usr/bin/env python3
"""Validate and compare DEADFLASH/Rufus benchmark JSONL records.

The comparator fails closed when correctness or hardware context differs. It
never converts an unequal comparison into a speed claim.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import random
import statistics
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from typing import Any, Iterable

SCHEMA = "deadflash.tool-comparison.run.v1"
SUMMARY_SCHEMA = "deadflash.tool-comparison.summary.v1"
REQUIRED_FIELDS = (
    "schema",
    "run_id",
    "tool",
    "tool_version",
    "device_id",
    "image_sha256",
    "image_bytes",
    "port_id",
    "controller",
    "os_build",
    "correctness_class",
    "conditioning",
    "order_index",
    "success",
    "total_ms",
    "evidence_path",
)
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
ALLOWED_CLASSES = {"A_WRITE_ONLY", "B_WRITE_FLUSH", "C_SAMPLE_VERIFY", "D_FULL_VERIFY"}


class DatasetError(ValueError):
    pass


@dataclass(frozen=True)
class Run:
    raw: dict[str, Any]
    line_number: int

    @property
    def tool(self) -> str:
        return str(self.raw["tool"])

    @property
    def success(self) -> bool:
        return bool(self.raw["success"])

    @property
    def total_ms(self) -> float:
        return float(self.raw["total_ms"])

    @property
    def image_bytes(self) -> int:
        return int(self.raw["image_bytes"])

    @property
    def throughput_mib_s(self) -> float:
        return (self.image_bytes / 1048576.0) / (self.total_ms / 1000.0)


def load_jsonl(path: pathlib.Path) -> list[Run]:
    runs: list[Run] = []
    seen_ids: set[str] = set()
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            try:
                record = json.loads(stripped)
            except json.JSONDecodeError as exc:
                raise DatasetError(f"line {line_number}: invalid JSON: {exc}") from exc
            if not isinstance(record, dict):
                raise DatasetError(f"line {line_number}: record must be a JSON object")
            missing = [field for field in REQUIRED_FIELDS if field not in record]
            if missing:
                raise DatasetError(f"line {line_number}: missing fields: {', '.join(missing)}")
            if record["schema"] != SCHEMA:
                raise DatasetError(f"line {line_number}: unsupported schema {record['schema']!r}")
            run_id = str(record["run_id"])
            if not run_id or run_id in seen_ids:
                raise DatasetError(f"line {line_number}: empty or duplicate run_id {run_id!r}")
            seen_ids.add(run_id)
            if not isinstance(record["success"], bool):
                raise DatasetError(f"line {line_number}: success must be true or false")
            if not isinstance(record["image_bytes"], int) or record["image_bytes"] <= 0:
                raise DatasetError(f"line {line_number}: image_bytes must be a positive integer")
            if not isinstance(record["order_index"], int) or record["order_index"] < 1:
                raise DatasetError(f"line {line_number}: order_index must be a positive integer")
            total_ms = record["total_ms"]
            if not isinstance(total_ms, (int, float)) or isinstance(total_ms, bool):
                raise DatasetError(f"line {line_number}: total_ms must be numeric")
            if record["success"] and (not math.isfinite(float(total_ms)) or float(total_ms) <= 0.0):
                raise DatasetError(f"line {line_number}: successful total_ms must be positive and finite")
            if record["correctness_class"] not in ALLOWED_CLASSES:
                raise DatasetError(
                    f"line {line_number}: correctness_class must be one of {sorted(ALLOWED_CLASSES)}"
                )
            digest = str(record["image_sha256"]).lower()
            if len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest):
                raise DatasetError(f"line {line_number}: image_sha256 must be 64 lowercase hex characters")
            record["image_sha256"] = digest
            runs.append(Run(record, line_number))
    if not runs:
        raise DatasetError("dataset contains no records")
    return runs


def validate_context(runs: list[Run], tool_names: tuple[str, str], min_success: int) -> dict[str, Any]:
    tools = {run.tool for run in runs}
    expected = set(tool_names)
    if tools != expected:
        raise DatasetError(f"dataset tools {sorted(tools)} do not equal required tools {sorted(expected)}")

    contexts: dict[str, set[str]] = {field: set() for field in CONTEXT_FIELDS}
    for run in runs:
        for field in CONTEXT_FIELDS:
            contexts[field].add(json.dumps(run.raw[field], sort_keys=True, separators=(",", ":")))
    mismatched = {field: sorted(values) for field, values in contexts.items() if len(values) != 1}
    if mismatched:
        details = "; ".join(f"{field}={values}" for field, values in mismatched.items())
        raise DatasetError(f"comparison context mismatch: {details}")

    indexes = [int(run.raw["order_index"]) for run in runs]
    if len(indexes) != len(set(indexes)):
        raise DatasetError("order_index values must be unique")
    if sorted(indexes) != list(range(1, len(indexes) + 1)):
        raise DatasetError("order_index values must form a complete 1..N sequence")

    counts = Counter(run.tool for run in runs)
    if abs(counts[tool_names[0]] - counts[tool_names[1]]) > 1:
        raise DatasetError(f"run allocation is not balanced: {dict(counts)}")

    success_counts = Counter(run.tool for run in runs if run.success)
    for tool in tool_names:
        if success_counts[tool] < min_success:
            raise DatasetError(
                f"{tool} has {success_counts[tool]} successful runs; minimum is {min_success}"
            )

    versions: dict[str, set[str]] = defaultdict(set)
    for run in runs:
        versions[run.tool].add(str(run.raw["tool_version"]))
    changing_versions = {tool: sorted(values) for tool, values in versions.items() if len(values) != 1}
    if changing_versions:
        raise DatasetError(f"tool version changed inside dataset: {changing_versions}")

    return {field: runs[0].raw[field] for field in CONTEXT_FIELDS}


def stats(values: Iterable[float]) -> dict[str, float]:
    data = list(values)
    if not data:
        raise DatasetError("cannot summarize an empty sample")
    return {
        "minimum": min(data),
        "median": statistics.median(data),
        "maximum": max(data),
        "mean": statistics.fmean(data),
        "sample_stddev": statistics.stdev(data) if len(data) > 1 else 0.0,
    }


def percentile(sorted_values: list[float], quantile: float) -> float:
    if not sorted_values:
        raise DatasetError("cannot calculate percentile of empty sample")
    position = (len(sorted_values) - 1) * quantile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[lower]
    fraction = position - lower
    return sorted_values[lower] * (1.0 - fraction) + sorted_values[upper] * fraction


def bootstrap_delta(
    left: list[float], right: list[float], seed_material: bytes, iterations: int
) -> dict[str, float]:
    seed = int.from_bytes(hashlib.sha256(seed_material).digest()[:8], "big")
    rng = random.Random(seed)
    deltas: list[float] = []
    for _ in range(iterations):
        left_sample = [left[rng.randrange(len(left))] for _ in left]
        right_sample = [right[rng.randrange(len(right))] for _ in right]
        left_median = statistics.median(left_sample)
        right_median = statistics.median(right_sample)
        deltas.append(((left_median / right_median) - 1.0) * 100.0)
    deltas.sort()
    return {
        "iterations": iterations,
        "seed": seed,
        "lower_95_percent": percentile(deltas, 0.025),
        "median": percentile(deltas, 0.5),
        "upper_95_percent": percentile(deltas, 0.975),
    }


def build_summary(
    runs: list[Run], tool_names: tuple[str, str], context: dict[str, Any], input_path: pathlib.Path,
    bootstrap_iterations: int,
) -> dict[str, Any]:
    grouped: dict[str, list[Run]] = {
        tool: [run for run in runs if run.tool == tool] for tool in tool_names
    }
    tool_summaries: dict[str, Any] = {}
    successful_throughput: dict[str, list[float]] = {}

    for tool in tool_names:
        all_runs = grouped[tool]
        successful = [run for run in all_runs if run.success]
        failed = [run for run in all_runs if not run.success]
        throughputs = [run.throughput_mib_s for run in successful]
        successful_throughput[tool] = throughputs
        tool_summaries[tool] = {
            "version": all_runs[0].raw["tool_version"],
            "runs_total": len(all_runs),
            "runs_successful": len(successful),
            "runs_failed": len(failed),
            "failure_rate": len(failed) / len(all_runs),
            "total_ms": stats(run.total_ms for run in successful),
            "effective_mib_s": stats(throughputs),
            "failed_run_ids": [str(run.raw["run_id"]) for run in failed],
            "evidence_paths": [str(run.raw["evidence_path"]) for run in all_runs],
        }

    left, right = tool_names
    left_median = tool_summaries[left]["effective_mib_s"]["median"]
    right_median = tool_summaries[right]["effective_mib_s"]["median"]
    observed_delta = ((left_median / right_median) - 1.0) * 100.0
    seed_material = input_path.read_bytes() + f"{left}\0{right}".encode()
    confidence = bootstrap_delta(
        successful_throughput[left], successful_throughput[right], seed_material, bootstrap_iterations
    )

    if confidence["lower_95_percent"] > 0:
        scoped_result = f"{left} had higher effective median throughput in this dataset"
    elif confidence["upper_95_percent"] < 0:
        scoped_result = f"{right} had higher effective median throughput in this dataset"
    else:
        scoped_result = "this dataset does not establish a stable median-throughput difference"

    return {
        "schema": SUMMARY_SCHEMA,
        "input": str(input_path),
        "context": context,
        "comparison_order": [left, right],
        "tools": tool_summaries,
        "median_effective_throughput_delta_percent": {
            "formula": f"(({left}_median / {right}_median) - 1) * 100",
            "observed": observed_delta,
            "bootstrap_95_percent": confidence,
        },
        "scoped_result": scoped_result,
        "claim_boundary": (
            "Valid only for this image, device, port, controller, OS build, conditioning, "
            "and correctness class. It is not a broad reliability or performance claim."
        ),
    }


def write_text_report(summary: dict[str, Any], path: pathlib.Path) -> None:
    left, right = summary["comparison_order"]
    context = summary["context"]
    delta = summary["median_effective_throughput_delta_percent"]
    lines = [
        "DEADFLASH TOOL COMPARISON",
        "=========================",
        "",
        f"IMAGE SHA-256      : {context['image_sha256']}",
        f"IMAGE BYTES        : {context['image_bytes']}",
        f"DEVICE ID          : {context['device_id']}",
        f"PORT ID            : {context['port_id']}",
        f"CONTROLLER         : {context['controller']}",
        f"OS BUILD           : {context['os_build']}",
        f"CORRECTNESS CLASS  : {context['correctness_class']}",
        f"CONDITIONING       : {context['conditioning']}",
        "",
    ]
    for tool in (left, right):
        item = summary["tools"][tool]
        lines.extend(
            [
                tool.upper(),
                "-" * len(tool),
                f"VERSION            : {item['version']}",
                f"RUNS               : {item['runs_total']}",
                f"SUCCEEDED          : {item['runs_successful']}",
                f"FAILED             : {item['runs_failed']}",
                f"MEDIAN TOTAL MS    : {item['total_ms']['median']:.3f}",
                f"MEDIAN EFFECTIVE   : {item['effective_mib_s']['median']:.3f} MiB/s",
                "",
            ]
        )
    ci = delta["bootstrap_95_percent"]
    lines.extend(
        [
            "COMPARISON",
            "----------",
            f"{left} VS {right} MEDIAN DELTA : {delta['observed']:.3f}%",
            f"BOOTSTRAP 95% RANGE            : {ci['lower_95_percent']:.3f}% .. {ci['upper_95_percent']:.3f}%",
            f"SCOPED RESULT                  : {summary['scoped_result']}",
            "",
            summary["claim_boundary"],
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=pathlib.Path, help="JSONL raw run records")
    parser.add_argument("--left-tool", default="DEADFLASH")
    parser.add_argument("--right-tool", default="Rufus")
    parser.add_argument("--minimum-successful-runs", type=int, default=5)
    parser.add_argument("--bootstrap-iterations", type=int, default=10000)
    parser.add_argument("--output-json", type=pathlib.Path, required=True)
    parser.add_argument("--output-text", type=pathlib.Path, required=True)
    parser.add_argument("--require-evidence-files", action="store_true")
    args = parser.parse_args()

    if args.minimum_successful_runs < 2:
        parser.error("--minimum-successful-runs must be at least 2")
    if args.bootstrap_iterations < 1000:
        parser.error("--bootstrap-iterations must be at least 1000")
    if args.left_tool == args.right_tool:
        parser.error("left and right tools must differ")

    try:
        runs = load_jsonl(args.input)
        if args.require_evidence_files:
            missing = [
                str(run.raw["evidence_path"])
                for run in runs
                if not pathlib.Path(str(run.raw["evidence_path"])).is_file()
            ]
            if missing:
                raise DatasetError(f"missing evidence files: {missing}")
        tool_names = (args.left_tool, args.right_tool)
        context = validate_context(runs, tool_names, args.minimum_successful_runs)
        summary = build_summary(runs, tool_names, context, args.input, args.bootstrap_iterations)
    except (OSError, DatasetError, ValueError) as exc:
        print(f"COMPARISON REJECTED: {exc}", file=sys.stderr)
        return 1

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_text.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    write_text_report(summary, args.output_text)
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
