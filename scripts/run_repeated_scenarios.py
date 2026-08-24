#!/usr/bin/env python3
"""Run the five shell scenarios repeatedly and save their output locally."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


REPO_DIR = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = REPO_DIR / "scripts"


def run_script(name: str, iteration: int, *arguments: str, output_dir: Path) -> tuple[int, Path, int]:
    """Run one shell scenario, capturing stdout and stderr in its log file."""
    log_file = output_dir / f"{name}-{iteration:02d}.log"
    command = [str(SCRIPTS_DIR / f"run_{name.replace('-', '_')}_scenario.sh"), *arguments]
    started_at = time.monotonic()

    with log_file.open("w", encoding="utf-8") as log:
        completed = subprocess.run(command, cwd=REPO_DIR, stdout=log, stderr=subprocess.STDOUT)

    duration_seconds = int(time.monotonic() - started_at)
    return completed.returncode, log_file, duration_seconds


def table_is_in_section(log_file: Path, phase: str, table_name: str) -> bool:
    """Return whether one table appears in an observer's initial/final list."""
    in_section = False
    target = f"- {table_name}"

    for line in log_file.read_text(encoding="utf-8").splitlines():
        if line.startswith(f"observer: {phase} table list:"):
            in_section = True
            continue

        if in_section and line.startswith("observer: after "):
            return False

        if in_section and line == target:
            return True

    return False


def created_table_name(log_file: Path) -> str | None:
    matches = re.findall(r"^writer: created (\S+)$", log_file.read_text(encoding="utf-8"), re.MULTILINE)
    return matches[-1] if matches else None


def log_contains(log_file: Path, expected: str) -> bool:
    return expected in log_file.read_text(encoding="utf-8").splitlines()


def indexed_cache_was_used(log_file: Path) -> bool:
    pattern = re.compile(r"^  index: hits=\d+ misses=\d+ entries=[1-9]\d* bytes=[1-9]\d*$")
    return any(pattern.match(line) for line in log_file.read_text(encoding="utf-8").splitlines())


def record_result(summary, scenario: str, iteration: int, status: str, duration: int, log_file: Path) -> bool:
    summary.write(f"{scenario}\t{iteration}\t{status}\t{duration}\t{log_file.name}\n")
    print(f"{scenario:<20} run {iteration:02d}: {status}")
    return status == "PASS"


def run_repetitions(run_count: int, output_dir: Path) -> int:
    output_dir.mkdir(parents=True, exist_ok=False)
    summary_path = output_dir / "summary.tsv"
    failures = 0

    with summary_path.open("w", encoding="utf-8") as summary:
        summary.write("scenario\titeration\tstatus\tduration_seconds\tlog\n")

        for iteration in range(1, run_count + 1):
            exit_code, create_log, duration = run_script("create", iteration, output_dir=output_dir)
            table_name = created_table_name(create_log)
            create_passed = (
                exit_code == 0
                and table_name is not None
                and table_is_in_section(create_log, "final", table_name)
            )
            if not record_result(summary, "create", iteration, "PASS" if create_passed else "FAIL", duration, create_log):
                failures += 1

            if table_name is None:
                drop_log = output_dir / f"drop-{iteration:02d}.log"
                drop_log.write_text("SKIP: create run did not produce a table name\n", encoding="utf-8")
                if not record_result(summary, "drop", iteration, "SKIP", 0, drop_log):
                    failures += 1
            else:
                exit_code, drop_log, duration = run_script("drop", iteration, table_name, output_dir=output_dir)
                drop_passed = (
                    exit_code == 0
                    and table_is_in_section(drop_log, "initial", table_name)
                    and not table_is_in_section(drop_log, "final", table_name)
                )
                if not record_result(summary, "drop", iteration, "PASS" if drop_passed else "FAIL", duration, drop_log):
                    failures += 1

            exit_code, append_log, duration = run_script("vector-append", iteration, output_dir=output_dir)
            append_passed = (
                exit_code == 0
                and log_contains(append_log, "observer: initial row count: 0")
                and log_contains(append_log, "observer: final row count: 20")
            )
            if not record_result(summary, "vector-append", iteration, "PASS" if append_passed else "FAIL", duration, append_log):
                failures += 1

            exit_code, delete_log, duration = run_script("vector-delete", iteration, output_dir=output_dir)
            delete_passed = (
                exit_code == 0
                and log_contains(delete_log, "observer: initial row count: 10")
                and log_contains(delete_log, "observer: final row count: 5")
            )
            if not record_result(summary, "vector-delete", iteration, "PASS" if delete_passed else "FAIL", duration, delete_log):
                failures += 1

            exit_code, indexed_log, duration = run_script("indexed-query-delete", iteration, output_dir=output_dir)
            indexed_passed = (
                exit_code == 0
                and log_contains(indexed_log, "observer: initial nearest id: 0")
                and log_contains(indexed_log, "observer: final nearest id: 1")
                and indexed_cache_was_used(indexed_log)
            )
            if not record_result(
                summary,
                "indexed-query-delete",
                iteration,
                "PASS" if indexed_passed else "FAIL",
                duration,
                indexed_log,
            ):
                failures += 1

    print(f"\nLogs: {output_dir}")
    print(f"Summary: {summary_path}")

    if failures:
        print(f"Failures: {failures}", file=sys.stderr)
        return 1

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_count", nargs="?", type=int, default=10)
    parser.add_argument("output_dir", nargs="?", type=Path)
    args = parser.parse_args()

    if args.run_count < 1:
        parser.error("run_count must be positive")

    if args.output_dir is None:
        args.output_dir = REPO_DIR / "test-runs" / datetime.now().strftime("%Y%m%d-%H%M%S")

    return args


if __name__ == "__main__":
    arguments = parse_args()
    raise SystemExit(run_repetitions(arguments.run_count, arguments.output_dir))
