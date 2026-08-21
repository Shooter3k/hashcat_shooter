#!/usr/bin/env python3
"""Train a compact Shooter PCFG model from an authorized plaintext corpus.

The model intentionally contains only aggregate counts converted to integer
negative-log scores. It never embeds hashes or any target-specific data.
"""

from __future__ import annotations

import argparse
import math
from collections import Counter, defaultdict
from pathlib import Path


SCORE_SCALE = 1_000_000


def byte_class(value: int) -> str:
    if 65 <= value <= 90:
        return "U"
    if 97 <= value <= 122:
        return "L"
    if 48 <= value <= 57:
        return "D"
    return "S"


def split_password(password: bytes) -> tuple[tuple[str, ...], tuple[tuple[str, bytes], ...]]:
    runs: list[tuple[str, bytes]] = []

    start = 0
    current = byte_class(password[0])

    for pos in range(1, len(password)):
        next_class = byte_class(password[pos])

        if next_class == current:
            continue

        value = password[start:pos]
        runs.append((f"{current}{len(value)}", value))

        start = pos
        current = next_class

    value = password[start:]
    runs.append((f"{current}{len(value)}", value))

    return tuple(token for token, _ in runs), tuple(runs)


def score(count: int, total: int) -> int:
    return round(-math.log2(count / total) * SCORE_SCALE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Train the deterministic PCFG model consumed by Hashcat's -a 8 pcfg feed."
    )
    parser.add_argument("input", type=Path, help="plaintext training corpus, one password per line")
    parser.add_argument("output", type=Path, help="model file to create")
    parser.add_argument(
        "--max-length",
        type=int,
        default=64,
        metavar="N",
        help="ignore passwords longer than N bytes (default: 64, maximum: 256)",
    )
    parser.add_argument(
        "--min-count",
        type=int,
        default=1,
        metavar="N",
        help="keep structures and terminals observed at least N times (default: 1)",
    )
    parser.add_argument(
        "--max-structures",
        type=int,
        default=50_000,
        metavar="N",
        help="maximum structures retained by frequency (default: 50000)",
    )
    parser.add_argument(
        "--max-terminals-per-token",
        type=int,
        default=100_000,
        metavar="N",
        help="maximum strings retained for each class/length token (default: 100000)",
    )

    args = parser.parse_args()

    if not 1 <= args.max_length <= 256:
        parser.error("--max-length must be between 1 and 256")
    if args.min_count < 1:
        parser.error("--min-count must be at least 1")
    if args.max_structures < 1:
        parser.error("--max-structures must be at least 1")
    if args.max_terminals_per_token < 1:
        parser.error("--max-terminals-per-token must be at least 1")

    return args


def main() -> int:
    args = parse_args()

    structures: Counter[tuple[str, ...]] = Counter()
    terminals: dict[str, Counter[bytes]] = defaultdict(Counter)

    accepted = 0
    empty = 0
    too_long = 0

    with args.input.open("rb") as source:
        for raw_line in source:
            password = raw_line.rstrip(b"\r\n")

            if not password:
                empty += 1
                continue
            if len(password) > args.max_length:
                too_long += 1
                continue

            structure, runs = split_password(password)

            structures[structure] += 1

            for token, value in runs:
                terminals[token][value] += 1

            accepted += 1

    if accepted == 0:
        raise SystemExit("training corpus contains no usable passwords")

    kept_structures = [
        (structure, count)
        for structure, count in structures.items()
        if count >= args.min_count
    ]
    kept_structures.sort(key=lambda item: (-item[1], item[0]))
    kept_structures = kept_structures[: args.max_structures]

    needed_tokens = {token for structure, _ in kept_structures for token in structure}

    kept_terminals: dict[str, list[tuple[bytes, int]]] = {}

    for token in sorted(needed_tokens):
        entries = [
            (value, count)
            for value, count in terminals[token].items()
            if count >= args.min_count
        ]
        entries.sort(key=lambda item: (-item[1], item[0]))
        entries = entries[: args.max_terminals_per_token]

        if entries:
            kept_terminals[token] = entries

    kept_structures = [
        (structure, count)
        for structure, count in kept_structures
        if all(token in kept_terminals for token in structure)
    ]

    if not kept_structures:
        raise SystemExit("training limits removed every structure")

    structure_total = sum(structures.values())

    args.output.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("wb") as model:
        model.write(b"SHOOTER-PCFG\t1\n")
        model.write(b"# T <token> <integer negative-log score> <terminal bytes as hex>\n")
        model.write(b"# S <reserved> <integer negative-log score> <comma-separated tokens>\n")

        for token in sorted(kept_terminals):
            token_total = sum(terminals[token].values())

            for value, count in kept_terminals[token]:
                record = f"T\t{token}\t{score(count, token_total)}\t{value.hex()}\n"
                model.write(record.encode("ascii"))

        scored_structures = [
            (score(count, structure_total), structure, count)
            for structure, count in kept_structures
        ]
        scored_structures.sort(key=lambda item: (item[0], item[1]))

        for structure_score, structure, _ in scored_structures:
            record = f"S\t\t{structure_score}\t{','.join(structure)}\n"
            model.write(record.encode("ascii"))

    terminal_count = sum(len(values) for values in kept_terminals.values())

    print(f"accepted passwords : {accepted}")
    print(f"ignored empty      : {empty}")
    print(f"ignored too long   : {too_long}")
    print(f"structures written : {len(kept_structures)}")
    print(f"terminals written  : {terminal_count}")
    print(f"model              : {args.output}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
