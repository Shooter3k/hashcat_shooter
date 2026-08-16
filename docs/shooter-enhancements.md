# Shooter enhancement details

This page explains every publicly documented difference summarized at the
beginning of the main README. The comparison baseline is upstream hashcat commit
[`fdad9f2f7`](https://github.com/hashcat/hashcat/commit/fdad9f2f7bd7ec7f53056727e39331a17514db7c).
Items 14 and 39 are newer official hashcat work included after that baseline;
they are not claimed as Shooter-authored features. Release-by-release evidence
and test results are preserved in [CHANGELOG.md](../CHANGELOG.md).

## Performance and startup

### 1. Parallel hash-list parsing

Compatible native-text inputs containing at least 4,194,304 nonempty hashes
are memory-mapped and decoded with up to 64 CPU workers. Unsupported or
malformed inputs automatically use hashcat's original parser. On the measured
84,381,739-hash MD5 list, parsing plus sorting fell from 33.56 to 6.41 seconds.
See [startup optimization](startup-optimization.md).

### 2. Parallel hash-list sorting

Unsalted lists containing at least 4,194,304 hashes can use a stable parallel
radix sort with up to 64 CPU workers. Smaller, salted, or allocation-limited
jobs retain the original comparison sorter. See
[startup optimization](startup-optimization.md).

### 3. Automatic CUDA-only fast start

Normal Windows cracking sessions skip redundant HIP and OpenCL discovery when
CUDA confirms the intended system has exactly twelve RTX 4090 GPUs. Backend
diagnostics still inspect every backend, and an environment variable disables
the shortcut. See [startup optimization](startup-optimization.md).

### 4. Concurrent GPU setup and shutdown

CUDA contexts, memory queries, candidate-buffer preparation, and device
teardown can run concurrently on multi-GPU sessions instead of processing each
GPU one at a time. See [startup optimization](startup-optimization.md).

### 5. Lower host memory use

On the exact Windows 12 x RTX 4090 configuration, Shooter limits the two host
candidate-staging slots to 3072 MiB per GPU and avoids unnecessary buffer
initialization. The measured fast-hash host commitment fell from about 97.7 GB
to 36.7 GB. See [startup optimization](startup-optimization.md).

### 6. Reusable RTX 4090 autotuning

Shooter saves successful RTX 4090 workload settings, validates them before
reuse, and shares matching settings across identical cards. Explicit tuning
options still win, and unsupported jobs bypass the cache. See the
[RTX 4090 autotune-cache guide](../RTX_4090_AUTOTUNE_CACHE.md).

### 7. Blowfish kernel caching

Compiled kernels can be reused for Blowfish-based modes that would otherwise
be rebuilt. This covers modes 3200, 25600, 25800, 28400, 30600, 30601, 33800,
and 35500.

### 8. High-volume result streaming

Recovered plaintexts can be reconstructed from retained host candidates
instead of copied back from the GPU for every result. Outfile and potfile
writes are processed in bounded groups of 4,096 results while remaining live
and flushed. A controlled all-cracked workload improved by about 360 times;
that I/O-bound result is not a universal speed guarantee. See the
[release notes](../CHANGELOG.md#v712-shooter2026081429).

## Candidate generation

### 9. Multi-file combination attacks

Attack mode 1 accepts two or more wordlists and joins one entry from every
file in command-line order. This replaces the earlier fixed private modes for
three through six files and also permits additional files. See the
[multi-file combination guide](multi-file-combination.md).

### 10. Efficient multi-GPU combination starts

For three-or-more-file combination attacks, each GPU converts its assigned
Cartesian offset directly into wordlist positions. A later GPU does not replay
all combinations assigned to earlier devices. See the
[multi-file combination guide](multi-file-combination.md).

### 11. Whole-candidate rules

`-r` and `-g` can transform the fully assembled candidate in attack modes 1,
3, 6, and 7. Existing `-j` and `-k` side rules still run before concatenation.
See the [whole-candidate rule guide](whole-candidate-rules.md).

### 12. Parallel stdout rule generation

High-volume straight wordlist-and-rule `--stdout` jobs use a reusable CPU
worker pool with up to 64 workers. Output remains deterministic, and the path
avoids GPU transfers that do not contribute to candidate generation.

### 13. Resumable stdout sessions

Mask- and file-driven `--stdout` sessions support pause, resume, checkpoint,
and quit. With a regular `-o` file, restore data binds the candidate position
to an exact byte boundary and removes an uncommitted tail before continuing.
See the [stdout session guide](stdout-sessions.md).

### 14. Official attack mode 12

Shooter includes official hashcat attack mode 12, added upstream after the
fork. Its mask can place one or two wordlist entries at arbitrary positions
using `?w` and `?q`. This is included functionality, not a Shooter-authored
feature. See the [integration release notes](../CHANGELOG.md#v712-shooter2026081218).

## Multibyte input

### 15. Multibyte masks work on Windows

Literal two-, three-, and four-byte UTF-8 characters work beside normal mask
tokens in every mask-capable hash mode. For example,
`?d?d№?d?d№?d?d№` reaches the mask engine without Windows replacing `№`.
See the [release notes](../CHANGELOG.md#v712-shooter2026081434).

### 16. Multibyte rules work on Windows

Rule files can use UTF-8 literals with the byte-emitting `$`, `^`, `i`, `v`,
and `o` operations. UTF-8 BOM-prefixed files, Unicode paths, and literal
Windows inline `-j` and `-k` rules are also supported. Existing rule positions
and non-emitting transforms remain byte-oriented. See the
[release notes](../CHANGELOG.md#v712-shooter2026081431).

## Long-job control and recovery

### 17. Interactive runtime controls

The interactive menu can extend a `--runtime` countdown or make it decrease at
twice normal speed. A running job can switch directly between the two modes.
See the [runtime-control guide](runtime-controls.md).

### 18. Coordinated multi-GPU checkpoints

A checkpoint request parks active GPUs at safe restore positions until every
participating device reaches the barrier. Cancelling the checkpoint releases
all parked devices and preserves prefetched candidate work. See the
[checkpoint-control guide](checkpoint-control.md).

### 19. Atomic CUDA startup retry

If context creation fails on any selected CUDA GPU, Shooter releases the
partial attempt and retries the complete session instead of continuing with a
subset of devices. Stream and event creation failures retry on the affected
context. Retries are bounded and end with an explicit error.

### 20. Windows outfile lock recovery

Startup validation and result-time append operations retry temporary Windows
outfile access failures every 250 milliseconds for up to five seconds. A
cooldown prevents persistent locks from adding the same long delay for every
later result. See the [release notes](../CHANGELOG.md#v712-shooter202608127).

### 21. Ignore outfile-check-dir

When `--outfile-check-dir` is active, `[i]gnore outfile-check-dir` stops further
directory checking for the current process without deleting or changing
files. Press `i` to activate it; the command then disappears from the prompt.
Hashes already processed remain recovered. See the
[release notes](../CHANGELOG.md#v712-shooter202608129-local-source-build).

### 22. Clear outfile-check completion status

When every recovered hash came from `--outfile-check-dir` rather than the
current attack, the final status says `cracked from outfile-check-dir`.

### 23. Reliable loopback cleanup

Windows loopback feeds release their file mappings before consumed induction
files are deleted. Cleanup failures are reported instead of letting the same
file be rediscovered indefinitely, and active files are preserved on abort or
quit. See the [release notes](../CHANGELOG.md#v712-shooter2026081431).

## Clearer status information

### 24. Visible quit progress

After `q` or `Q`, Shooter reports candidate and GPU drain, worker completion,
session-service shutdown, GPU-resource release, and final session-file work so
the console does not appear frozen. See the
[release notes](../CHANGELOG.md#v712-shooter2026081213-local-source-build).

### 25. Total run time

The final summary prints `Total Run Time`, calculated from the displayed
`Started` and `Stopped` timestamps. See the
[release notes](../CHANGELOG.md#v712-shooter202608125).

### 26. Correct combination status

A two-wordlist mode-1 attack using whole-candidate rules reports the real left
and right wordlist paths instead of a `(null)` feed label. See the
[release notes](../CHANGELOG.md#v712-shooter2026081215).

### 27. Visible Pure Kernel warning

Interactive terminals display the complete Pure Kernel status line in bright
yellow. Redirected, logged, and machine-readable output remains plain text.
See the [release notes](../CHANGELOG.md#v712-shooter2026081325).

## Hash formats and compatibility

### 28. Complete mdxfind namespace

Shooter exposes every name in mdxfind's live registry as `e1` through `e1001`.
Of those 1,001 entries, 999 are self-contained algorithms with passing test
vectors. `e426` is a scheduler pseudo-entry, and `e535` requires external
mdxfind custom-user/salt state. See the
[mdxfind compatibility guide](mdxfind-modules.md) and
[complete registry](mdxfind-modules.json).

### 29. Public mdxfind mode names

Help, hash information, examples, runtime status, benchmarks, autodetection,
diagnostics, and session logs show public `eN` names instead of private numeric
plugin identifiers. Existing standard numeric modes remain unchanged.

### 30. Magento Argon2 input

Mode `e987` accepts standard Argon2 PHC strings and mdxfind's Magento
`hex_digest:salt:2` and extended `:3_...` forms. The original Magento line is
retained for potfiles, `--show`, `--left`, and cracked output. See the
[release notes](../CHANGELOG.md#v712-shooter2026081322).

### 31. phpBB3 bcrypt-over-phpass modes

Mode `29950` handles `bcrypt(phpass($pass))`; mode `29951` explicitly handles
the rarer `bcrypt(phpass(md5($pass)))` construction. Both accept the original
phpBB3 record and run both stages through hashcat's GPU scheduler. See the
[mode 29950 guide](mode-29950.md).

### 32. Mode 29980

Mode `29980` implements the supported libxcrypt-style gost-yescrypt
`$gy$j9T$` profile on the GPU. See the
[gost-yescrypt notes](../GOST_YESCRYPT_GPU.md).

### 33. Mode 67000

Mode `67000` is a compatibility number for older yescrypt jobs and uses the
maintained implementation behind current mode `36100`. New jobs should use
`36100`. See the [mode 67000 guide](mode-67000.md).

## Downloads and builds

### 34. Complete Windows release archive

Each release publishes one `shooter_hashcat-<version>-windows-x64-complete.7z`
containing the complete
tagged source, the ready-to-run Windows x64 executable, module and bridge DLLs,
required runtime DLLs, build metadata, and rebuild tools.

### 35. Package integrity verification

The archive includes `SHA256SUMS` covering its source and binary contents plus
`verify-windows-package.ps1`, which checks every manifest entry after
extraction.

### 36. Self-bootstrapping Windows build

`build-windows.ps1` downloads a checksum-pinned MSYS2 toolchain into the local
`.build-tools` directory and builds Shooter without installing system-wide
software or changing the user or system `PATH`. See
[how_to_compile.txt](../how_to_compile.txt).

### 37. Portable Windows build instructions

Build commands work with a fresh clone on any drive and include all required
GCC, Clang, Rust, OpenSSL, iconv, bridge, feed, and runtime dependencies. No
developer-specific absolute paths are required. See
[how_to_compile.txt](../how_to_compile.txt).

### 38. Reproducible release versioning

Production source pins its release date and revision so later rebuilds retain
the same version across machines and time zones. Packaging and release
automation refuse to publish an executable that does not exactly match the
requested tag.

## Newer official work included after the fork

### 39. Newer upstream correctness fixes

Shooter synchronized with official hashcat through commit
[`eba388d2e`](https://github.com/hashcat/hashcat/commit/eba388d2ef8d2dc6f184cb2effdc1a99493d888d).
In addition to the earlier feed, yescrypt, PDF, combinator, overflow,
double-free, and bounds fixes, the current merge includes the shared plugin
core and ABI checks, LM parsing and corruption fixes, secp256k1 zero-scalar
and bounds fixes, updated zlib, Windows CI, and Android build corrections.
These fixes are included but not claimed as Shooter-authored work.

## Rule loading

### 40. Parallel rule-file loading

Plain rule files of at least 16 MiB are read once and validated with up to 64
CPU workers. This is part of the shared rule loader, so it benefits every hash
algorithm and attack that accepts `-r`; it is not limited to MD5. Rule order,
comments, blank lines, UTF-8 BOM handling, LF/CRLF input, invalid-rule
warnings, and chained rule files retain their existing behavior. Compressed
rule files and unusual inputs automatically keep the original streaming path.

The loader also avoids one allocation and free for every rule, grows its
serial fallback buffer geometrically, and no longer allocates and copies a
second complete compiled-rule array when one rule file is used. On the
measured 60.80 MiB file containing 4,902,480 rules, loader-only startup fell
from 26.235 seconds with the pre-change binary to a seven-run median of 0.140
seconds. See [startup optimization](startup-optimization.md#large-rule-file-parsing)
for controls, memory behavior, and verification details.

## Optional status detail

### 41. Optional Restore.Sub status lines

The per-device `Restore.Sub` rows are hidden from normal human-readable status
output by default, keeping a 12-GPU status screen twelve lines shorter. Add
`--status-restore-sub` to show the original salt, amplifier, and iteration
ranges:

```powershell
hashcat.exe ... --status-restore-sub
```

The switch covers manual status requests, automatic `--status` updates, final
summaries, every selected GPU, and bridge-backed modes. `Restore.Point` remains
visible either way, and hiding the rows does not change checkpoint or restore
behavior. JSON and machine-readable status formats are unchanged because they
did not emit these human-readable rows.

## Portable release binaries

### 42. Portable prebuilt Windows binaries

The one-file Windows release is compiled for the standard x64 baseline instead
of the particular CPU model used by GitHub Actions. This prevents a downloaded
`hashcat.exe`, bridge, or feed DLL from failing with an illegal-instruction
error on an older x64 processor. The repo-local `build-windows.ps1` wrapper
uses the same portable setting, while developers can still invoke make
directly when they intentionally want machine-specific optimization.

## Potfile reporting

### 43. Faster show and left

`--show` and `--left` use narrower searches when comparing a potfile with a
large hash list. Unsalted lists use a small 16-bit prefix index over the
already-sorted hashes. Salted lists first locate the matching salt group and
then search only that group's digests. Custom potfile validators and the
special keep-all-hashes path retain their original matching behavior.

When `-o` is supplied, each selected line is already written by Hashcat's
outfile writer. Shooter no longer makes a second heap-allocated copy of every
line, sorts all of those unused copies, and frees them one at a time. This is
especially important for `--left`, where nearly the complete input list may
need to be emitted. Standard-output mode retains original input ordering.

No new option is required. See
[startup optimization](startup-optimization.md#large-show-and-left-workloads)
for measured results, scope, and verification details.

## Linux builds

### 44. Working Linux builds

Shooter's bundled mdxfind libraries are compiled as position-independent code
before they are linked into `bridge_mdxfind.so`. This fixes the Linux linker
failure that previously reported an `R_X86_64_PC32` relocation in
`librhash.a` and requested a rebuild with `-fPIC`.

Both the normal `SHARED=0` build and the `SHARED=1` library build are covered.
See [BUILD.md](../BUILD.md) for the tested Ubuntu prerequisites and commands.

## Support and troubleshooting

### 45. Automatic error reports

The first normal Hashcat error in a process creates a uniquely named
`shooter_hashcat-error-YYYYMMDD-HHMMSS-PID.log` in the directory where the
program was started. Every later error and warning from the same run is
appended to that one file, including errors reported concurrently by different
GPU workers. Up to 64 recent warnings from before the first error are included
as context. This ensures messages such as `Hash parsing error`, which Hashcat
internally treats as warnings, are not lost. Successful runs and warning-only
runs do not create a report.

The report includes timestamps, the exact Shooter version, operating system,
architecture, process ID, working directory, and a bounded command-line
argument list. `--brain-password` values are automatically replaced with
`[REDACTED]`. Input and output files are not attached, but a normal diagnostic
can quote an individual malformed input line. Other arguments and paths may
also be private, so users should review the text file before sharing it. See
the [error-report guide](error-reports.md).

## Testing and companion operations

### 46. Sanitizers and parser fuzzing

The Security workflow compiles the shared parser core with AddressSanitizer
and UndefinedBehaviorSanitizer and runs a coverage-guided libFuzzer target over
the common tokenizer and multibyte rule compiler. Scheduled and change-based
runs retain any crash input as a workflow artifact. See
[security-testing.md](security-testing.md).

### 47. `shooterctl` companion

Normal Windows and Linux builds now produce `shooterctl` beside Hashcat. It is
a dependency-free Rust companion for installation checks, support bundles,
large text indexes, rule reports, target manifests, streaming, GPU work
distribution, and mode discovery. The complete release contains both its
source and prebuilt executable. See [shooterctl.md](shooterctl.md).

### 48. Doctor and support bundles

`shooterctl doctor` checks the neighboring Hashcat version, plugin directories,
backend discovery, and optional tools. Human and JSON output are available.
`support-bundle` writes a manually requested environment bundle that excludes
command lines, hashes, candidates, potfile contents, and environment values,
and redacts the home-directory prefix. See
[the companion guide](shooterctl.md#doctor-and-privacy-safe-support-bundles).

### 49. Stage time and peak memory

`--stage-profile` prints final time for feed, copy, initialization, temporary
transfer, main launch, and comparison stages together with measured launches
and peak process RAM. `--stage-profile-json` emits the same fields as one
`shooter-stage-profile-v1` object. Profiling is disabled by default. See
[stage-profile.md](stage-profile.md).

### 50. Rule-list intelligence

`shooterctl rule-report` reads multi-gigabyte files with bounded memory and
reports selected ranges, comments, blank lines, multibyte and invalid UTF-8
lines, longest rule, common operation bytes, per-file order, and a fixed-memory
duplicate estimate. Manifests can run several rule files sequentially instead
of multiplying them together. See
[the rule-report guide](shooterctl.md#rule-reports-ranges-and-rule-series).

### 51. Persistent line indexes

`shooterctl index` creates a `.hcidx` sidecar for a line-oriented wordlist,
rule list, potfile, or similar file. It stores source identity, counts,
fingerprint, and sampled offsets with bounded memory. `shooterctl stream`
automatically uses a valid sidecar to jump near a requested or restored line;
stale indexes are ignored. See
[the index guide](shooterctl.md#persistent-hcidx-indexes).

### 52. Streaming pipeline

`shooterctl stream` handles stdin, regular files, zstd decoding, line ranges,
periodic resume checkpoints, explicit progress counts, and `.hcidx` seek
acceleration. `shooterctl pipeline` directly connects a mask, hybrid,
combination, or ruled `--stdout` producer to a Hashcat stdin consumer without
a shell or temporary candidate file. See
[the streaming guide](shooterctl.md#partial-compressed-and-resumable-streams).

### 53. Target manifests and import

The `shooter-target-v1` JSON format records a mode, attack type, hash source,
wordlists, masks, rules, output, potfile, total work, and extra options.
`manifest import-command` converts an existing common Hashcat command;
`show`, `plan`, and `run` make it reviewable and repeatable. See
[the manifest guide](shooterctl.md#target-manifests-and-command-import).

### 54. Adaptive GPU fleet

Fleet mode splits an explicitly sized job into a shared queue for up to all
twelve GPUs. Devices take another chunk as they finish, failed chunks are
requeued, and repeatedly failing devices are quarantined. JSON Lines telemetry
records duration and effective work rate for every chunk. See
[the fleet guide](shooterctl.md#adaptive-multi-gpu-fleet).

### 55. Mode search and explanation

`shooterctl mode search`, `mode explain`, and `mode identify` query the
installed binary rather than a hard-coded list. They cover both standard
numeric modes and Shooter's complete mdxfind `eN` namespace. See
[the mode guide](shooterctl.md#mode-search-explanation-and-identification).

### 56. SBOM and signed release attestations

The Windows package contains an SPDX 2.3 inventory of the executables and
plugin DLLs with SHA-256 checksums and principal dependencies. The release
workflow creates Sigstore-backed GitHub attestations for both build provenance
and the SBOM while retaining one public `.7z` download asset. See
[release-security.md](release-security.md).

## Attack automation

### 57. Automatic low-crack-rate bypass

`--bypass-delay` and `--bypass-threshold` work together to measure how many
new hashes the current dictionary or mask cracks during each time window. If
the number is below the threshold, Hashcat selects the next queued dictionary
or mask. A single-entry queue ends with `Status: Bypass`. Already-recovered
hashes do not inflate the measurement, and time spent paused does not consume
the delay window. See [bypass-rate-control.md](bypass-rate-control.md).

## Outfile-check startup

### 58. Outfile-check before cracking allocation

Before building bitmap tables, loading the candidate source, initializing
attack kernels, allocating attack-specific GPU and host buffers, or running
autotune, Shooter compares the loaded hashes with every existing result in
`--outfile-check-dir`. Matching hashes are marked recovered immediately. If
the directory accounts for the complete target list, Shooter exits without
starting the cracking allocation. If hashes remain, only those remaining
hashes continue into the normal attack.

The same file positions are handed to the live outfile watcher, so a partial
preflight does not immediately reread unchanged files from the beginning.
`--outfile-check-timer=0` disables both this preflight and the live watcher.
Modes that intentionally disable outfile checking retain that behavior.

The directory must contain only trusted recovered-result files. Hashcat's
existing outfile reader scans the files in that directory and intentionally
accepts entries with or without a plaintext, so a target hash list must not be
stored there.

Like Hashcat's existing all-found potfile shortcut, the check happens after
the lightweight backend-runtime and device-enumeration phase needed by the
current session architecture. It avoids the much larger per-attack GPU and
host allocation; it does not suppress the initial device list. See
[startup optimization](startup-optimization.md#existing-outfile-results-before-cracking-allocation).

## Wordlist I/O

### 59. Faster large-wordlist indexing and feed

The first time Shooter sees a wordlist, it builds the sparse line index used
for multi-GPU distribution, `--skip`, and restore. Very large files now use up
to 64 CPU workers and SIMD newline counting, allowing the storage queue and
the available processor cores to work together. The new byte-spaced index is
also much smaller. Existing seek databases still load, so no cache cleanup or
conversion step is required.

During cracking, each wordlist reader caches its selected SIMD line scanner.
The common path also avoids the complete candidate-transform function when no
inline rule, encoding conversion, hexadecimal input mode, or forced-uppercase
mode is active and an ordinary candidate cannot be `$HEX[...]`. Actual
`$HEX[...]` entries and every configured transformation retain their original
behavior.

No option is required. On the supplied 60,256,380,643-byte email list, the
first-use index pass fell from 57.376 seconds to 11.756 seconds, or 4.88 times
faster. The optimized pass read at 4.77 GiB/s, about 90 percent of a separate
5.28 GiB/s sequential-read measurement on the same system, while the index
shrank from 4,343,248 bytes to 229,904 bytes. A live 12-GPU mode-0 test with
112 loaded rules processed 4,060,086,272 base words in its 10-second cracking
window versus 2,919,628,800 before the final feed optimizations, a 39.1 percent
increase. See [startup optimization](startup-optimization.md#large-wordlist-indexing-and-feed-throughput)
for scope and verification details.

## Status visibility

### 60. Consistent remaining and recovery-rate status

Every normal human-readable status display includes both of these lines:

```text
Remaining........: 33473 (99.93%) Digests
Recovered/Time...: CUR:N/A,N/A,N/A AVG:N/A,N/A,N/A (Min,Hour,Day)
```

They are no longer suppressed when a job contains 1,000 or fewer digests.
`Remaining` reports the live digest count and percentage; jobs with multiple
salts continue to include the salt count and percentage on the same line.
`Recovered/Time` uses `N/A` for time windows that have not elapsed yet, then
fills in the minute, hour, and day rates as those windows become available.

This affects the normal terminal display, including periodic and final status.
Machine-readable and JSON status formats keep their existing structured
fields and formats.
