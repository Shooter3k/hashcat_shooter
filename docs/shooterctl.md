# `shooterctl` companion

`shooterctl` is installed beside `hashcat.exe` on Windows and beside `hashcat`
on Linux. It handles repeatable job plans, diagnostics, very large text-file
utilities, stream orchestration, and multi-process GPU work distribution while
leaving Hashcat's normal command line unchanged.

Run `shooterctl --help` for the command summary. Commands print the Hashcat
commands they execute so a plan can be reviewed before it is started.

## Doctor and privacy-safe support bundles

```powershell
shooterctl.exe doctor
shooterctl.exe doctor --json
shooterctl.exe support-bundle support-check
```

`doctor` verifies the installed Hashcat executable, version, module, bridge,
and feed directories, backend discovery, and optional zstd availability. A
support bundle contains `doctor.json`, `backend-info.txt`, and a privacy notice.
It deliberately omits command lines, hashes, candidates, potfile contents, and
environment-variable values. Paths below the current home directory are
replaced with `<HOME>`. Users should still review every file before sharing it.

This is separate from the automatic error report described in
[error-reports.md](error-reports.md): the automatic report captures a failing
run, while a doctor bundle is a manually requested environment check.

## Persistent `.hcidx` indexes

```powershell
shooterctl.exe index C:\path\to\wordlists\large.txt
shooterctl.exe index --stride 16384 C:\path\to\wordlists\large.txt
```

The indexer reads a file once with bounded memory and writes
`large.txt.hcidx`. The index records source size, modification time, line
count, a full-file fingerprint, and sampled byte offsets. It works with any
line-oriented file, including wordlists, rules, and potfiles.

`shooterctl stream` automatically uses a matching index to seek close to a
requested `--skip-lines` or saved checkpoint instead of rereading every prior
line. A stale index is ignored safely. Compressed `.zst` files cannot be byte
seeked and therefore resume by decoding and skipping input.

The index is a sidecar optimization. Hashcat can still open the source without
it, and deleting an index never deletes or changes its source file.

## Rule reports, ranges, and rule series

```powershell
shooterctl.exe rule-report C:\path\to\rules\large.rule
shooterctl.exe rule-report --json --skip 1000000 --limit 500000 rules.rule
```

The report streams through multi-gigabyte rule sets with a 4 MiB reader. It
shows the selected line and byte counts, empty and comment lines, multibyte and
invalid UTF-8 lines, the longest rule, file order, common rule bytes, and a
fixed-memory estimate of possible duplicates. The duplicate count is a Bloom
filter estimate and can include false positives; it does not modify the file.

A target manifest may list several rule files. `shooterctl plan` and
`shooterctl run` treat those as a series of independent Hashcat jobs in the
listed order. That is useful when files should run one after another. It does
not chain the files into a Cartesian product as repeated `-r` options do.

## Target manifests and command import

```powershell
shooterctl.exe manifest import-command job.json -- hashcat.exe -m 0 -a 0 hashes.txt words.txt -r rules.rule
shooterctl.exe manifest show job.json
shooterctl.exe plan job.json
shooterctl.exe run job.json
```

The `shooter-target-v1` JSON format records the hash mode, attack mode, hash
source, wordlists, masks, rules, output, potfile, optional total work, and
additional Hashcat arguments. The importer recognizes the common short, long,
and `--option=value` forms and keeps unrecognized options in `extra` rather
than discarding them. Review `plan` output after importing a complicated
command.

`manifest create` is also available for scripts that prefer named fields:

```powershell
shooterctl.exe manifest create job.json --mode 0 --attack-mode 0 `
  --hashes hashes.txt --wordlist words.txt --rule first.rule --rule second.rule
```

## Partial, compressed, and resumable streams

```powershell
shooterctl.exe stream candidates.zst --zstd --checkpoint run.position `
  --skip-lines 1000000 --limit-lines 5000000 --total-candidates 5000000 `
  -- hashcat.exe-arguments-here
```

The stream command accepts a regular file, `-` for its own standard input, or
a zstd file. It can select a line range, write a resumable source-position
checkpoint every 10,000 candidates, and report progress against an explicitly
declared count. The declared count is companion metadata; it is not confused
with Hashcat's existing count-and-exit `--total-candidates` switch.

For attacks that generate candidates before another Hashcat process tests
them, `pipeline` connects both processes directly without a shell or temporary
candidate file:

```powershell
shooterctl.exe pipeline -- `
  -a 6 words.txt ?d?d `
  ::: -m 0 target-hashes.txt
```

The producer receives `--stdout` automatically. Its output becomes the
consumer's standard input, so combination, hybrid, mask, and ruled producers
can feed a normal stdin cracking job. Failure in either process is returned as
a failure from `shooterctl`.

## Adaptive multi-GPU fleet

```powershell
shooterctl.exe fleet job.json --devices 1,2,3,4,5,6,7,8,9,10,11,12 `
  --chunk-size 100000000 --retries 2 --telemetry fleet.jsonl
```

Fleet mode divides the manifest's required `total_work` into a shared queue.
Each selected device runs one chunk at a time with `--skip`, `--limit`, and a
device-specific session. A device that finishes early takes the next queued
chunk, so work is redistributed dynamically. Failed chunks are requeued; a
device that repeatedly fails is quarantined while healthy devices continue.

The JSON Lines telemetry records chunk start and finish events, duration,
effective work per second, retry outcomes, and quarantine events. Output paths
may contain `{device}` and `{start}` placeholders. Potfiles are separated per
device to avoid concurrent writers.

Fleet mode starts one Hashcat process per device. This intentionally trades
additional hash-list parsing and host memory for fault isolation and dynamic
redistribution; measure memory first before using it with extremely large hash
lists. Manifests with several rule files should use `run`, or separate fleet
runs, because fleet accepts at most one rule file per invocation.

## Mode search, explanation, and identification

```powershell
shooterctl.exe mode search magento
shooterctl.exe mode explain e987
shooterctl.exe mode identify hashes.txt
```

Mode search filters the installed Hashcat registry by number, name, category,
or descriptive text. `explain` prints the complete installed `--hash-info`
block for one standard or mdxfind mode. `identify` runs Hashcat's parser-based
mode identification on one hash or a file. Results come from the binary beside
`shooterctl`, so they match the installed module set rather than a hard-coded
web list.

## Building the companion

The normal `make` target builds `shooterctl` after Hashcat, modules, bridges,
and feeds. It uses stable Rust but has no third-party Cargo dependencies.
`make clean` removes its build products. The complete Windows archive contains
both its source and ready-to-run executable.
