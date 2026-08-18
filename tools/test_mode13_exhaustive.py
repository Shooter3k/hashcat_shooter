#!/usr/bin/env python3

"""Exhaustively compare bounded mode-13 stage orders with a small oracle.

Every W/M/R type string from length one through --max-stages is tested.  Each
stage has exactly two choices, which keeps the complete six-stage suite to
1,092 processes and 55,986 emitted candidates per binary while still testing
every structural order in that bound.

Pass a normal build as --host-binary.  During development, a build that allows
the mode-13 GPU suffix compiler under --stdout can be supplied as
--compiled-binary; stdout's CPU rule emulator then verifies the exact composite
kernel rules without requiring thousands of GPU session startups.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import itertools
import os
import pathlib
import subprocess
import tempfile
from dataclasses import dataclass


STAGE_TYPES = "WMR"
WORD_VALUES = (b"a", b"B")
MASK_VALUES = (b"0", b"1")


@dataclass(frozen=True)
class Fixtures:
    wordlist: pathlib.Path
    rules: pathlib.Path


def expected_candidates(pattern: str) -> list[bytes]:
    candidates = [b""]

    for stage in pattern:
        if stage == "W":
            candidates = [prefix + value for prefix in candidates for value in WORD_VALUES]
        elif stage == "M":
            candidates = [prefix + value for prefix in candidates for value in MASK_VALUES]
        elif stage == "R":
            candidates = [value for prefix in candidates for value in (prefix, prefix.upper())]
        else:  # pragma: no cover - patterns are generated locally
            raise AssertionError(stage)

    return candidates


def device_for_pattern(pattern: str, devices: int) -> int:
    value = (len(STAGE_TYPES) ** len(pattern) - len(STAGE_TYPES)) // (len(STAGE_TYPES) - 1)

    for stage in pattern:
        value = value * len(STAGE_TYPES) + STAGE_TYPES.index(stage)

    return value % devices + 1


def command_for(
    binary: pathlib.Path,
    fixtures: Fixtures,
    pattern: str,
    devices: int,
    shooterctl: pathlib.Path | None,
) -> list[str]:
    hashcat_args = [
        "--stdout",
        "--quiet",
        "--restore-disable",
        "--logfile-disable",
        "--session",
        f"mode13-exhaustive-{os.getpid()}-{binary.stem[-12:]}-{pattern}",
        "--markov-disable",
        "--backend-ignore-opencl",
        "-a",
        "13",
    ]

    if "M" in pattern:
        hashcat_args[hashcat_args.index("-a"):hashcat_args.index("-a")] = ["-1", "01"]

    for stage in pattern:
        if stage == "W":
            hashcat_args.append(str(fixtures.wordlist))
        elif stage == "M":
            hashcat_args.append("?1")
        else:
            hashcat_args.extend(("-r", str(fixtures.rules)))

    if shooterctl is not None:
        return [
            str(shooterctl),
            "worker",
            "run",
            "--hashcat",
            str(binary),
            "--",
            *hashcat_args,
        ]

    hashcat_args[hashcat_args.index("-a"):hashcat_args.index("-a")] = [
        "-d",
        str(device_for_pattern(pattern, devices)),
    ]

    return [str(binary), *hashcat_args]


def run_pipeline(
    binary: pathlib.Path,
    fixtures: Fixtures,
    pattern: str,
    devices: int,
    shooterctl: pathlib.Path | None,
) -> list[bytes]:
    result = subprocess.run(
        command_for(binary, fixtures, pattern, devices, shooterctl),
        cwd=binary.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=120,
    )

    if result.returncode != 0:
        diagnostic = result.stderr.decode("utf-8", "replace")[-2000:]
        raise AssertionError(f"{binary.name} {pattern}: exit {result.returncode}\n{diagnostic}")

    return result.stdout.splitlines()


def verify_pattern(
    host_binaries: tuple[pathlib.Path, ...],
    compiled_binaries: tuple[pathlib.Path, ...],
    fixtures: Fixtures,
    pattern: str,
    pattern_index: int,
    devices: int,
    shooterctl: pathlib.Path | None,
) -> tuple[str, int]:
    expected = expected_candidates(pattern)

    binaries = [host_binaries[pattern_index % len(host_binaries)]]

    if compiled_binaries:
        binaries.append(compiled_binaries[pattern_index % len(compiled_binaries)])

    for binary in binaries:
        actual = run_pipeline(binary, fixtures, pattern, devices, shooterctl)

        if actual != expected:
            mismatch = next(
                (
                    index
                    for index, pair in enumerate(itertools.zip_longest(actual, expected))
                    if pair[0] != pair[1]
                ),
                min(len(actual), len(expected)),
            )
            got = actual[mismatch] if mismatch < len(actual) else b"<missing>"
            want = expected[mismatch] if mismatch < len(expected) else b"<missing>"
            raise AssertionError(
                f"{binary.name} {pattern}: candidate {mismatch}: got {got!r}, expected {want!r}; "
                f"counts {len(actual)}/{len(expected)}"
            )

    return pattern, len(expected)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host-binary", required=True, action="append", type=pathlib.Path)
    parser.add_argument("--compiled-binary", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--shooterctl", type=pathlib.Path)
    parser.add_argument("--max-stages", type=int, default=6)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 1))
    parser.add_argument("--devices", type=int, default=1)

    args = parser.parse_args()

    if args.max_stages < 1:
        parser.error("--max-stages must be at least one")
    if args.jobs < 1:
        parser.error("--jobs must be at least one")
    if args.devices < 1:
        parser.error("--devices must be at least one")

    return args


def main() -> int:
    args = parse_args()

    host_binaries = tuple(binary.resolve() for binary in args.host_binary)
    compiled_binaries = tuple(binary.resolve() for binary in args.compiled_binary)
    binaries = host_binaries + compiled_binaries

    shooterctl = args.shooterctl.resolve() if args.shooterctl is not None else None

    for binary in binaries:
        if not binary.is_file():
            raise SystemExit(f"binary does not exist: {binary}")

    if shooterctl is not None and not shooterctl.is_file():
        raise SystemExit(f"shooterctl does not exist: {shooterctl}")

    patterns = [
        "".join(stages)
        for length in range(1, args.max_stages + 1)
        for stages in itertools.product(STAGE_TYPES, repeat=length)
    ]

    with tempfile.TemporaryDirectory(prefix="hashcat-mode13-exhaustive-") as temp_name:
        temp = pathlib.Path(temp_name)
        fixtures = Fixtures(temp / "words.txt", temp / "rules.rule")

        fixtures.wordlist.write_bytes(b"a\nB\n")
        fixtures.rules.write_bytes(b":\nu\n")

        candidate_count = 0

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = {
                executor.submit(
                    verify_pattern,
                    host_binaries,
                    compiled_binaries,
                    fixtures,
                    pattern,
                    pattern_index,
                    args.devices,
                    shooterctl,
                ): pattern
                for pattern_index, pattern in enumerate(patterns)
            }

            for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
                pattern, count = future.result()
                candidate_count += count

                if completed % 100 == 0 or completed == len(patterns):
                    print(f"verified {completed}/{len(patterns)} structural pipelines", flush=True)

    print(
        f"PASS: {len(patterns)} pipelines and {candidate_count} ordered candidates "
        f"per semantic path across {len(host_binaries)} host and "
        f"{len(compiled_binaries)} compiled worker binary/binaries"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
