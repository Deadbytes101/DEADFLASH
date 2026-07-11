#!/usr/bin/env python3
"""Deterministic MSVC qualification harness for DEADFLASH."""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence


EXPECTED_TESTS = 7
SCHEMA = "deadflash.msvc-qualification.v1"
VS_COMPONENT = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"


class QualificationError(RuntimeError):
    pass


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def utc_text() -> str:
    return utc_now().isoformat().replace("+00:00", "Z")


def timestamp_text() -> str:
    return utc_now().strftime("%Y%m%dT%H%M%SZ")


def resolve_path(value: str | None, base: Path, default: Path) -> Path:
    if not value:
        return default.resolve()
    path = Path(value)
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def write_json_atomic(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def executable_path(name: str) -> Path | None:
    found = shutil.which(name)
    return Path(found).resolve() if found else None


def version_key(path: Path) -> tuple[int, ...]:
    values: list[int] = []
    for piece in re.split(r"[^0-9]+", path.name):
        if piece:
            values.append(int(piece))
    return tuple(values)


def run_capture(command: Sequence[str], *, purpose: str) -> str:
    try:
        completed = subprocess.run(
            command,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise QualificationError(f"{purpose} failed to start: {exc}") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise QualificationError(
            f"{purpose} failed with exit code {completed.returncode}: {detail}"
        )
    return completed.stdout.strip()


def discover_visual_studio() -> dict[str, Any]:
    candidates: list[Path] = []
    for variable in ("ProgramFiles(x86)", "ProgramFiles"):
        root = os.environ.get(variable)
        if root:
            candidate = Path(root) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
            if candidate.is_file() and candidate not in candidates:
                candidates.append(candidate)

    cl_from_path = executable_path("cl.exe")
    if not candidates:
        if cl_from_path is None:
            raise QualificationError(
                "Visual Studio discovery failed: neither vswhere.exe nor cl.exe was found."
            )
        return {
            "source": "existing-environment",
            "installation_path": None,
            "installation_version": None,
            "major_version": 0,
            "vswhere": None,
            "cl": str(cl_from_path),
            "bundled_cmake": None,
            "bundled_ctest": None,
        }

    vswhere = candidates[0]
    print(f"[DISCOVER] {vswhere}", flush=True)
    common = [
        str(vswhere),
        "-latest",
        "-products",
        "*",
        "-requires",
        VS_COMPONENT,
    ]
    installation_path_text = run_capture(
        [*common, "-property", "installationPath"],
        purpose="vswhere installationPath query",
    ).splitlines()
    if not installation_path_text:
        raise QualificationError("No Visual Studio installation with the x64 C++ toolchain was found.")
    installation = Path(installation_path_text[0].strip()).resolve()

    version_lines = run_capture(
        [*common, "-property", "installationVersion"],
        purpose="vswhere installationVersion query",
    ).splitlines()
    installation_version = version_lines[0].strip() if version_lines else None
    major_version = 0
    if installation_version:
        match = re.match(r"(\d+)", installation_version)
        if match:
            major_version = int(match.group(1))
    if major_version <= 0 and installation.name.isdigit():
        major_version = int(installation.name)

    tools_root = installation / "VC" / "Tools" / "MSVC"
    cl_candidates: list[Path] = []
    if tools_root.is_dir():
        for toolset in tools_root.iterdir():
            candidate = toolset / "bin" / "Hostx64" / "x64" / "cl.exe"
            if candidate.is_file():
                cl_candidates.append(candidate)
    cl_candidates.sort(key=lambda item: version_key(item.parent.parent.parent.parent), reverse=True)
    cl_path = cl_candidates[0] if cl_candidates else cl_from_path
    if cl_path is None:
        raise QualificationError(
            f"Visual Studio was found at '{installation}', but x64 cl.exe was not found."
        )

    cmake_root = (
        installation
        / "Common7"
        / "IDE"
        / "CommonExtensions"
        / "Microsoft"
        / "CMake"
        / "CMake"
        / "bin"
    )
    bundled_cmake = cmake_root / "cmake.exe"
    bundled_ctest = cmake_root / "ctest.exe"
    if not (bundled_cmake.is_file() and bundled_ctest.is_file()):
        bundled_cmake = None
        bundled_ctest = None

    return {
        "source": "vswhere",
        "installation_path": str(installation),
        "installation_version": installation_version,
        "major_version": major_version,
        "vswhere": str(vswhere),
        "cl": str(cl_path),
        "bundled_cmake": str(bundled_cmake) if bundled_cmake else None,
        "bundled_ctest": str(bundled_ctest) if bundled_ctest else None,
    }


def select_cmake_generator(help_text: str, visual_studio_major: int) -> str:
    matches = re.findall(
        r"(?m)^\s*\*?\s*(Visual Studio\s+(\d+)\s+[^=\r\n]+?)\s*=",
        help_text,
    )
    generators = [(name.strip(), int(major)) for name, major in matches]
    if not generators:
        raise QualificationError(
            "CMake did not report any Visual Studio generators in `cmake --help`."
        )
    if visual_studio_major > 0:
        for name, major in generators:
            if major == visual_studio_major:
                return name
        available = ", ".join(name for name, _ in generators)
        raise QualificationError(
            f"Installed Visual Studio major version is {visual_studio_major}, but this "
            f"CMake does not provide a matching generator. Available: {available}. "
            "Install a newer CMake build that supports the installed Visual Studio."
        )
    return max(generators, key=lambda item: item[1])[0]


def host_metadata() -> dict[str, Any]:
    return {
        "computer_name": os.environ.get("COMPUTERNAME"),
        "os_description": platform.platform(),
        "os_architecture": os.environ.get("PROCESSOR_ARCHITEW6432")
        or os.environ.get("PROCESSOR_ARCHITECTURE")
        or platform.machine(),
        "process_architecture": platform.machine(),
        "is_64_bit_process": sys.maxsize > 2**32,
        "python": platform.python_version(),
        "python_executable": sys.executable,
    }


class Recorder:
    def __init__(self, log_dir: Path, record: dict[str, Any]) -> None:
        self.log_dir = log_dir
        self.record = record

    def run(
        self,
        name: str,
        executable: Path | str,
        arguments: Iterable[str],
        *,
        cwd: Path,
        accepted: tuple[int, ...] = (0,),
        timeout: int = 900,
    ) -> dict[str, Any]:
        executable_text = str(executable)
        argument_list = [str(value) for value in arguments]
        command = [executable_text, *argument_list]
        stdout_path = self.log_dir / f"{name}.stdout.txt"
        stderr_path = self.log_dir / f"{name}.stderr.txt"

        print("", flush=True)
        print(f"[RUN] {name}", flush=True)
        print(f"      {subprocess.list2cmdline(command)}", flush=True)
        print(f"      stdout: {stdout_path}", flush=True)
        print(f"      stderr: {stderr_path}", flush=True)

        started = time.monotonic()
        return_code: int | None = None
        timed_out = False
        start_error: str | None = None

        creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        try:
            with stdout_path.open("wb") as stdout_handle, stderr_path.open("wb") as stderr_handle:
                process = subprocess.Popen(
                    command,
                    cwd=str(cwd),
                    stdin=subprocess.DEVNULL,
                    stdout=stdout_handle,
                    stderr=stderr_handle,
                    shell=False,
                    creationflags=creation_flags,
                )
                next_heartbeat = 5
                while True:
                    return_code = process.poll()
                    if return_code is not None:
                        break
                    elapsed = time.monotonic() - started
                    if elapsed >= timeout:
                        timed_out = True
                        print(
                            f"[TIMEOUT] {name} exceeded {timeout}s; terminating process tree.",
                            flush=True,
                        )
                        subprocess.run(
                            ["taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
                            check=False,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,
                        )
                        try:
                            return_code = process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            return_code = process.wait(timeout=5)
                        break
                    if elapsed >= next_heartbeat:
                        print(f"[RUNNING] {name} elapsed={elapsed:.0f}s", flush=True)
                        next_heartbeat += 5
                    time.sleep(1)
        except (OSError, subprocess.SubprocessError) as exc:
            start_error = str(exc)

        elapsed_ms = round((time.monotonic() - started) * 1000.0, 3)
        stdout = read_text(stdout_path)
        stderr = read_text(stderr_path)
        entry = {
            "name": name,
            "executable": executable_text,
            "arguments": argument_list,
            "working_directory": str(cwd),
            "exit_code": return_code,
            "timed_out": timed_out,
            "timeout_seconds": timeout,
            "elapsed_ms": elapsed_ms,
            "stdout": stdout,
            "stderr": stderr,
            "start_error": start_error,
        }
        self.record["commands"].append(entry)

        if start_error is not None:
            raise QualificationError(f"Command '{name}' could not be started: {start_error}")
        if timed_out:
            raise QualificationError(f"Command '{name}' timed out after {timeout} seconds.")
        if return_code not in accepted:
            if stdout.strip():
                print(f"--- {name} stdout ---\n{stdout}", flush=True)
            if stderr.strip():
                print(f"--- {name} stderr ---\n{stderr}", flush=True)
            raise QualificationError(
                f"Command '{name}' failed with exit code {return_code}."
            )

        print(
            f"[PASS] {name} exit={return_code} elapsed={elapsed_ms / 1000.0:.1f}s",
            flush=True,
        )
        return entry


def git_commit(source_dir: Path) -> str | None:
    git = executable_path("git.exe") or executable_path("git")
    if git is None:
        return None
    try:
        completed = subprocess.run(
            [str(git), "-C", str(source_dir), "rev-parse", "HEAD"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir")
    parser.add_argument("--build-dir")
    parser.add_argument("--evidence-dir")
    parser.add_argument("--keep-build", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    script_dir = Path(__file__).resolve().parent
    source_dir = resolve_path(arguments.source_dir, Path.cwd(), script_dir.parent)
    build_dir = resolve_path(
        arguments.build_dir,
        source_dir,
        source_dir / "build-msvc-qualification",
    )
    evidence_dir = resolve_path(
        arguments.evidence_dir,
        source_dir,
        source_dir / "bench" / "results",
    )
    if not source_dir.is_dir():
        print(f"MSVC QUALIFICATION: FAIL - Source directory does not exist: {source_dir}")
        return 1

    stamp = timestamp_text()
    log_dir = Path(os.environ.get("TEMP", str(Path.cwd()))) / f"deadflash-msvc-{stamp}"
    log_dir.mkdir(parents=True, exist_ok=True)
    evidence_dir.mkdir(parents=True, exist_ok=True)
    evidence_path = evidence_dir / f"msvc-qualification-{stamp}.json"
    e2e_path = evidence_dir / f"msvc-e2e-{stamp}.json"

    record: dict[str, Any] = {
        "schema": SCHEMA,
        "created_utc": utc_text(),
        "result": "running",
        "source_dir": str(source_dir),
        "build_dir": str(build_dir),
        "evidence_path": str(evidence_path),
        "log_directory": str(log_dir),
        "cmake_generator": None,
        "cmake_architecture": "x64",
        "expected_tests": EXPECTED_TESTS,
        "host": host_metadata(),
        "visual_studio_environment": None,
        "build_tools": None,
        "git_commit": None,
        "commands": [],
        "tests": None,
        "e2e": None,
        "error": None,
    }
    recorder = Recorder(log_dir, record)

    print("DEADFLASH MSVC QUALIFICATION", flush=True)
    print("============================", flush=True)
    print(f"SOURCE   : {source_dir}", flush=True)
    print(f"BUILD    : {build_dir}", flush=True)
    print(f"EVIDENCE : {evidence_path}", flush=True)
    print(f"LOGS     : {log_dir}", flush=True)

    try:
        print("\n[1/7] Discover Visual Studio and MSVC", flush=True)
        visual_studio = discover_visual_studio()
        record["visual_studio_environment"] = visual_studio
        print(f"[FOUND] Visual Studio source: {visual_studio['source']}", flush=True)
        print(
            f"[FOUND] Visual Studio version: {visual_studio['installation_version']}",
            flush=True,
        )
        print(f"[FOUND] cl.exe: {visual_studio['cl']}", flush=True)

        print("\n[2/7] Validate tools and select matching CMake generator", flush=True)
        if visual_studio["bundled_cmake"]:
            cmake = Path(visual_studio["bundled_cmake"])
            ctest = Path(visual_studio["bundled_ctest"])
            cmake_source = "visual-studio-bundled"
        else:
            cmake = executable_path("cmake.exe")
            ctest = executable_path("ctest.exe")
            cmake_source = "path"
            if cmake is None:
                raise QualificationError("Required executable is unavailable: cmake.exe")
            if ctest is None:
                raise QualificationError("Required executable is unavailable: ctest.exe")
        python = Path(sys.executable).resolve()
        record["build_tools"] = {
            "cmake_source": cmake_source,
            "cmake": str(cmake),
            "ctest": str(ctest),
            "python": str(python),
        }
        print(f"[FOUND] cmake.exe ({cmake_source}): {cmake}", flush=True)
        print(f"[FOUND] ctest.exe: {ctest}", flush=True)
        print(f"[FOUND] python.exe: {python}", flush=True)

        recorder.run("cmake-version", cmake, ["--version"], cwd=source_dir, timeout=60)
        cmake_help = recorder.run("cmake-help", cmake, ["--help"], cwd=source_dir, timeout=60)
        generator = select_cmake_generator(
            cmake_help["stdout"] + "\n" + cmake_help["stderr"],
            int(visual_studio["major_version"] or 0),
        )
        record["cmake_generator"] = generator
        print(f"[SELECTED] CMake generator: {generator}", flush=True)

        record["git_commit"] = git_commit(source_dir)
        if record["git_commit"]:
            print(f"[COMMIT] {record['git_commit']}", flush=True)

        if build_dir.exists() and not arguments.keep_build:
            print(f"[CLEAN] Removing {build_dir}", flush=True)
            shutil.rmtree(build_dir)
        build_dir.mkdir(parents=True, exist_ok=True)

        print("\n[3/7] Compile MSVC probe and record compiler version", flush=True)
        probe_source = log_dir / "cl-version-probe.c"
        probe_source.write_text(
            "int deadflash_msvc_probe(void) { return 0; }\n",
            encoding="ascii",
            newline="\n",
        )
        recorder.run(
            "cl-version",
            visual_studio["cl"],
            ["/nologo", "/Bv", "/TC", "/Zs", str(probe_source)],
            cwd=source_dir,
            timeout=60,
        )

        print(f"\n[4/7] Configure {generator} x64", flush=True)
        recorder.run(
            "cmake-configure",
            cmake,
            [
                "-S",
                str(source_dir),
                "-B",
                str(build_dir),
                "-G",
                generator,
                "-A",
                "x64",
                "-DDEADFLASH_WARNINGS_AS_ERRORS=ON",
                "-DDEADFLASH_BUILD_TESTS=ON",
            ],
            cwd=source_dir,
            timeout=300,
        )

        print("\n[5/7] Build Release", flush=True)
        recorder.run(
            "cmake-build",
            cmake,
            ["--build", str(build_dir), "--config", "Release", "--parallel"],
            cwd=source_dir,
            timeout=1800,
        )

        print("\n[6/7] Run all seven tests", flush=True)
        ctest_result = recorder.run(
            "ctest",
            ctest,
            ["--test-dir", str(build_dir), "-C", "Release", "--output-on-failure"],
            cwd=source_dir,
            timeout=600,
        )
        tests_passed = (
            "100% tests passed" in ctest_result["stdout"]
            and f"0 tests failed out of {EXPECTED_TESTS}" in ctest_result["stdout"]
        )
        summary = [
            line
            for line in ctest_result["stdout"].splitlines()
            if "tests passed" in line or "Total Test time" in line
        ]
        record["tests"] = {
            "expected": EXPECTED_TESTS,
            "passed": EXPECTED_TESTS if tests_passed else None,
            "raw_summary": summary,
        }
        if not tests_passed:
            raise QualificationError(
                "CTest exited successfully but did not report all seven tests passing."
            )

        deadflash = build_dir / "Release" / "deadflash.exe"
        proof = build_dir / "Release" / "deadflash-proof.exe"
        for binary in (deadflash, proof):
            if not binary.is_file():
                raise QualificationError(f"Expected Release binary is missing: {binary}")

        print("\n[7/7] Run proof/corruption end-to-end qualification", flush=True)
        e2e_work = build_dir / "e2e-msvc"
        recorder.run(
            "e2e-proof",
            python,
            [
                str(source_dir / "scripts" / "e2e-proof.py"),
                "--deadflash",
                str(deadflash),
                "--proof",
                str(proof),
                "--work-dir",
                str(e2e_work),
                "--summary",
                str(e2e_path),
            ],
            cwd=source_dir,
            timeout=900,
        )
        if not e2e_path.is_file():
            raise QualificationError(f"E2E evidence was not created: {e2e_path}")
        e2e = json.loads(e2e_path.read_text(encoding="utf-8-sig"))
        record["e2e"] = e2e
        if not (
            e2e.get("write_state") == "success_verified"
            and e2e.get("proof_state") == "success_proven"
            and e2e.get("corruption_state") == "target_mismatch"
            and e2e.get("injected_bad_offset") == e2e.get("reported_bad_offset")
        ):
            raise QualificationError(
                "MSVC E2E evidence does not satisfy the qualification contract."
            )

        record["result"] = "pass"
        print("\nMSVC QUALIFICATION: PASS", flush=True)
        return_code = 0
    except (QualificationError, OSError, ValueError, json.JSONDecodeError) as exc:
        record["result"] = "fail"
        record["error"] = {"type": type(exc).__name__, "message": str(exc)}
        print(f"\nMSVC QUALIFICATION: FAIL - {exc}", flush=True)
        return_code = 1
    except KeyboardInterrupt:
        record["result"] = "fail"
        record["error"] = {"type": "KeyboardInterrupt", "message": "Interrupted by user"}
        print("\nMSVC QUALIFICATION: FAIL - interrupted by user", flush=True)
        return_code = 130
    finally:
        record["completed_utc"] = utc_text()
        write_json_atomic(evidence_path, record)
        print(f"MSVC EVIDENCE: {evidence_path}", flush=True)
        print(f"MSVC LOGS: {log_dir}", flush=True)

    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
