#!/usr/bin/env python3
"""Destructive physical USB qualification harness for DEADFLASH.

Nothing is written unless --execute and an exact destruction phrase are both
present. The harness records every command, output, return code, and state.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
import time
from dataclasses import asdict, dataclass


WINDOWS_PHYSICAL = re.compile(r"^(?:\\\\\.\\|//\./)PhysicalDrive[0-9]+$", re.IGNORECASE)


@dataclass
class CommandRecord:
    argv: list[str]
    returncode: int
    elapsed_ms: float
    stdout: str
    stderr: str


def parse_fields(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        fields[key.strip().lower().replace(" ", "_")] = value.strip()
    return fields


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_physical_target(path: str) -> bool:
    if os.name == "nt":
        return WINDOWS_PHYSICAL.fullmatch(path) is not None
    return path.startswith("/dev/") and not path.startswith("/dev/mapper/")


def run(argv: list[str], accepted: set[int] | None = None) -> CommandRecord:
    accepted = {0} if accepted is None else accepted
    start = time.perf_counter()
    completed = subprocess.run(argv, text=True, capture_output=True, check=False)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    record = CommandRecord(argv, completed.returncode, elapsed_ms, completed.stdout, completed.stderr)
    if completed.returncode not in accepted:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {argv!r}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return record


def require_destructive_authorization(args: argparse.Namespace) -> None:
    if not is_physical_target(args.target):
        raise RuntimeError(f"refusing non-physical target syntax: {args.target!r}")
    phrase = f"DESTROY:{args.target}:{args.token}"
    if not args.execute:
        raise RuntimeError(f"dry gate active; rerun with --execute --destroy-phrase {phrase!r}")
    if args.destroy_phrase != phrase:
        raise RuntimeError(f"wrong destruction phrase; expected exactly {phrase!r}")


def base_record(args: argparse.Namespace) -> dict[str, object]:
    image = pathlib.Path(args.image).resolve()
    if not image.is_file():
        raise RuntimeError(f"image does not exist: {image}")
    return {
        "schema": "deadflash.usb-qualification.v1",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "phase": args.phase,
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": sys.version,
            "computer": platform.node(),
        },
        "image": str(image),
        "image_size": image.stat().st_size,
        "image_sha256": sha256_file(image),
        "target": args.target,
        "expected_token": args.token,
        "commands": [],
    }


def append(record: dict[str, object], command: CommandRecord) -> dict[str, str]:
    commands = record["commands"]
    assert isinstance(commands, list)
    commands.append(asdict(command))
    return parse_fields(command.stdout)


def inspect_target(args: argparse.Namespace, record: dict[str, object]) -> dict[str, str]:
    command = run([args.deadflash, "inspect", args.target])
    fields = append(record, command)
    actual = fields.get("confirmation_token")
    if actual != args.token:
        raise RuntimeError(f"target token changed: expected={args.token!r} actual={actual!r}")
    if fields.get("system_disk") == "YES" and not args.force_system_disk:
        raise RuntimeError("target is classified as the system disk")
    return fields


def policy_args(args: argparse.Namespace) -> list[str]:
    result = [
        "--allow-device",
        "--confirm",
        args.token,
        "--verify",
        args.verify,
        "--buffer",
        args.buffer,
    ]
    if args.direct:
        result.append("--direct")
    if args.force_mounted:
        result.append("--force-mounted")
    if args.force_system_disk:
        result.append("--force-system-disk")
    return result


def seal(args: argparse.Namespace, record: dict[str, object]) -> str:
    command = run([args.proof, "seal", args.image, args.target, *policy_args(args)])
    fields = append(record, command)
    value = fields.get("plan_seal", "")
    if len(value) != 64:
        raise RuntimeError(f"invalid plan seal: {value!r}")
    return value


def phase_baseline(args: argparse.Namespace, record: dict[str, object]) -> None:
    inspect_fields = inspect_target(args, record)
    plan_seal = seal(args, record)
    proof_path = pathlib.Path(args.output_dir) / "baseline.dfp"
    write = run(
        [
            args.proof,
            "write",
            args.image,
            args.target,
            "--seal",
            plan_seal,
            *policy_args(args),
            "--proof",
            str(proof_path),
            "--chunk",
            args.chunk,
        ]
    )
    write_fields = append(record, write)
    if write_fields.get("state") != "success_verified":
        raise RuntimeError(f"baseline write state is not success_verified: {write_fields!r}")
    if write_fields.get("proof_state") != "success_proven":
        raise RuntimeError(f"baseline proof state is not success_proven: {write_fields!r}")
    verify = run([args.proof, "verify", str(proof_path), args.image, args.target])
    verify_fields = append(record, verify)
    if verify_fields.get("state") != "success_proven":
        raise RuntimeError(f"offline proof state is not success_proven: {verify_fields!r}")
    record["target_inspection"] = inspect_fields
    record["plan_seal"] = plan_seal
    record["result"] = "pass"


def phase_unplug(args: argparse.Namespace, record: dict[str, object]) -> None:
    inspect_target(args, record)
    plan_seal = seal(args, record)
    argv = [
        args.proof,
        "write",
        args.image,
        args.target,
        "--seal",
        plan_seal,
        *policy_args(args),
    ]
    print(f"WRITE STARTS NOW. UNPLUG THE SACRIFICIAL DEVICE IN {args.unplug_delay:.1f} SECONDS.")
    start = time.perf_counter()
    process = subprocess.Popen(argv, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(args.unplug_delay)
    print("UNPLUG NOW.", flush=True)
    try:
        stdout, stderr = process.communicate(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()
        raise RuntimeError("writer did not exit after unplug timeout")
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    command = CommandRecord(argv, process.returncode, elapsed_ms, stdout, stderr)
    fields = append(record, command)
    if process.returncode == 0:
        raise RuntimeError("unplug phase unexpectedly returned success; unplug was not observed")
    if fields.get("state") in {"success_verified", "success_unverified"}:
        raise RuntimeError(f"unplug phase emitted a success state: {fields!r}")
    record["plan_seal"] = plan_seal
    record["observed_state"] = fields.get("state", "missing")
    record["result"] = "pass_fault_observed"


def phase_verify(args: argparse.Namespace, record: dict[str, object]) -> None:
    inspect_target(args, record)
    proof_path = pathlib.Path(args.manifest).resolve()
    if not proof_path.is_file():
        raise RuntimeError(f"proof manifest does not exist: {proof_path}")
    command = run(
        [args.proof, "verify", str(proof_path), args.image, args.target],
        accepted={0, 1},
    )
    fields = append(record, command)
    state = fields.get("state", "missing")
    record["observed_state"] = state
    record["result"] = "pass" if state in {"success_proven", "target_mismatch"} else "fail"
    if record["result"] != "pass":
        raise RuntimeError(f"unexpected post-cycle proof state: {state}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("phase", choices=("baseline", "unplug", "verify"))
    parser.add_argument("--deadflash", required=True)
    parser.add_argument("--proof", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--token", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--manifest")
    parser.add_argument("--buffer", default="32MiB")
    parser.add_argument("--chunk", default="4MiB")
    parser.add_argument("--verify", choices=("sample", "full"), default="full")
    parser.add_argument("--direct", action="store_true")
    parser.add_argument("--force-mounted", action="store_true")
    parser.add_argument("--force-system-disk", action="store_true")
    parser.add_argument("--unplug-delay", type=float, default=3.0)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--destroy-phrase", default="")
    args = parser.parse_args()

    if args.phase == "verify" and not args.manifest:
        parser.error("verify requires --manifest")
    require_destructive_authorization(args)

    output_dir = pathlib.Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    args.output_dir = str(output_dir)
    record = base_record(args)
    output_path = output_dir / f"{args.phase}-{int(time.time())}.json"

    try:
        if args.phase == "baseline":
            phase_baseline(args, record)
        elif args.phase == "unplug":
            phase_unplug(args, record)
        else:
            phase_verify(args, record)
    except Exception as exc:
        record["result"] = "fail"
        record["error"] = str(exc)
        output_path.write_text(json.dumps(record, indent=2), encoding="utf-8")
        print(f"QUALIFICATION FAILURE: {exc}", file=sys.stderr)
        print(f"RECORD: {output_path}", file=sys.stderr)
        return 1

    output_path.write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(json.dumps(record, indent=2))
    print(f"RECORD: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
