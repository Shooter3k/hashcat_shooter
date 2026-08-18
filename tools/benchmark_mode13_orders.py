#!/usr/bin/env python3

"""Benchmark every order of a representative four-stage mode-13 pipeline.

The stages are a large wordlist (W), a rule file (R), a mask (M), and a small
wordlist (D).  All 24 permutations are measured.  A resident Shooter worker is
recommended so backend startup does not dominate every short sample; start it
before running this script and stop it afterward.

The script refuses to start while any NVIDIA GPU is already busy.  Reported
utilization is the median of the five samples with the highest fleet-wide
utilization, which removes worker setup and teardown samples while retaining
the slow-order imbalance that this benchmark is intended to expose.
"""

from __future__ import annotations

import argparse
import itertools
import pathlib
import re
import statistics
import subprocess
import tempfile
import time


SPEED_RE = re.compile(r"Speed\.#\*\.{9}:\s+([0-9.]+)\s+([kMGT]?H)/s")
AMP_RE = re.compile(r"Guess\.GPU\.Amp\.{4}:\s+([0-9]+) candidates")
SPEED_FACTORS = {"H": 1.0, "kH": 1e3, "MH": 1e6, "GH": 1e9, "TH": 1e12}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=pathlib.Path)
    parser.add_argument("--shooterctl", type=pathlib.Path)
    parser.add_argument("--large-wordlist", required=True, type=pathlib.Path)
    parser.add_argument("--rules", required=True, type=pathlib.Path)
    parser.add_argument("--mask", default="?d")
    parser.add_argument("--suffix-wordlist", required=True, type=pathlib.Path)
    parser.add_argument("--hash", default="00000000000000000000000000000000")
    parser.add_argument("--runtime", type=int, default=4)
    parser.add_argument("--sample-ms", type=int, default=250)
    parser.add_argument("--idle-threshold", type=int, default=10)
    args = parser.parse_args()

    for path in (args.binary, args.large_wordlist, args.rules, args.suffix_wordlist):
        if not path.is_file():
            parser.error(f"file does not exist: {path}")
    if args.shooterctl is not None and not args.shooterctl.is_file():
        parser.error(f"file does not exist: {args.shooterctl}")
    if args.runtime < 2:
        parser.error("--runtime must be at least 2 seconds")
    if args.sample_ms < 100:
        parser.error("--sample-ms must be at least 100")

    return args


def gpu_utilization() -> list[int]:
    result = subprocess.run(
        [
            "nvidia-smi.exe",
            "--query-gpu=utilization.gpu",
            "--format=csv,noheader,nounits",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
        timeout=10,
    )
    if result.returncode != 0:
        raise RuntimeError(f"nvidia-smi failed: {result.stderr.strip()}")
    return [int(line.strip()) for line in result.stdout.splitlines() if line.strip()]


def command_for(args: argparse.Namespace, order: tuple[str, ...]) -> list[str]:
    stages = {
        "W": [str(args.large_wordlist.resolve())],
        "R": ["-r", str(args.rules.resolve())],
        "M": [args.mask],
        "D": [str(args.suffix_wordlist.resolve())],
    }
    hashcat_args = [
        "-m", "0",
        "-w", "4",
        "-a", "13",
        args.hash,
        "--potfile-disable",
        "--restore-disable",
        "--logfile-disable",
        "--hwmon-disable",
        "--backend-ignore-opencl",
        "--self-test-disable",
        "--status",
        "--status-timer", "1",
        "--runtime", str(args.runtime),
        "-O",
    ]
    for stage in order:
        hashcat_args.extend(stages[stage])

    if args.shooterctl is None:
        return [str(args.binary.resolve()), *hashcat_args]
    return [
        str(args.shooterctl.resolve()),
        "worker", "run",
        "--hashcat", str(args.binary.resolve()),
        "--",
        *hashcat_args,
    ]


def parse_speed(output: str) -> float:
    matches = SPEED_RE.findall(output)
    if not matches:
        raise RuntimeError("status output did not contain an aggregate speed")
    value, unit = matches[-1]
    return float(value) * SPEED_FACTORS[unit]


def peak_window(samples: list[list[int]]) -> list[float]:
    if not samples:
        return []
    selected = sorted(samples, key=sum, reverse=True)[: min(5, len(samples))]
    return [statistics.median(sample[gpu] for sample in selected) for gpu in range(len(selected[0]))]


def main() -> int:
    args = parse_args()
    binary = args.binary.resolve()

    baseline = gpu_utilization()
    if any(value > args.idle_threshold for value in baseline):
        raise SystemExit(
            "refusing a contaminated benchmark: GPU baseline is "
            + ",".join(str(value) for value in baseline)
            + "%"
        )

    results: list[tuple[str, int, float, float, float, float]] = []
    orders = list(itertools.permutations("WRMD"))

    print("order,amplifier,speed_GH_s,gpu_util_min,gpu_util_avg,gpu_util_max", flush=True)

    for order in orders:
        output_file = tempfile.TemporaryFile(mode="w+", encoding="utf-8", errors="replace")
        process = subprocess.Popen(
            command_for(args, order),
            cwd=binary.parent,
            stdin=subprocess.DEVNULL,
            stdout=output_file,
            stderr=subprocess.STDOUT,
            text=True,
        )
        samples: list[list[int]] = []

        while process.poll() is None:
            samples.append(gpu_utilization())
            time.sleep(args.sample_ms / 1000.0)

        process.wait(timeout=10)
        output_file.flush()
        output_file.seek(0)
        output = output_file.read()
        output_file.close()
        # Hashcat uses 4 for the expected --runtime stop.  A fully exhausted or
        # recovered tiny test can also return 0; some wrappers preserve 1.
        if process.returncode not in (0, 1, 4) or "Speed.#*" not in output:
            raise RuntimeError(
                f"{''.join(order)} exited {process.returncode}:\n{output[-3000:]}"
            )

        speed = parse_speed(output)
        amplifier_match = AMP_RE.findall(output)
        amplifier = int(amplifier_match[-1]) if amplifier_match else 1
        util = peak_window(samples)
        if not util:
            raise RuntimeError(f"{''.join(order)} produced no GPU utilization samples")

        row = (
            "".join(order),
            amplifier,
            speed / 1e9,
            min(util),
            statistics.mean(util),
            max(util),
        )
        results.append(row)
        print(
            f"{row[0]},{row[1]},{row[2]:.3f},{row[3]:.1f},{row[4]:.1f},{row[5]:.1f}",
            flush=True,
        )

    best = max(results, key=lambda row: row[2])
    fully_busy = [row[0] for row in results if row[3] >= 95.0]
    print(
        f"BEST: {best[0]} at {best[2]:.3f} GH/s, amplifier {best[1]}, "
        f"GPU utilization {best[3]:.1f}/{best[4]:.1f}/{best[5]:.1f}% min/avg/max"
    )
    print("ALL_GPUS_AT_LEAST_95_PERCENT: " + (",".join(fully_busy) or "none"))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
