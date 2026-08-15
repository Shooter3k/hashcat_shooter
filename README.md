# hashcat_shooter

`hashcat_shooter` is a Windows-focused version of hashcat 7.1.2. It keeps the
normal hashcat commands and features, then adds faster handling of very large
hash lists, better multi-GPU behavior, more ways to build password candidates,
Unicode support on Windows, additional hash types, and a ready-to-run release.

It was developed for a Windows machine with 12 NVIDIA GeForce RTX 4090 GPUs,
but most of its additions also work on other hardware.

Use this software only on passwords and systems you own or are explicitly
authorized to audit.

## Improvements at a glance

- **[Huge hash lists open faster](docs/startup-optimization.md)** — parallel
  CPU parsing and sorting reduce the wait at `Parsing Hashes`.
- **[Large GPU systems start faster and use less memory](docs/startup-optimization.md)**
  — especially the intended Windows system with 12 RTX 4090 GPUs.
- **[RTX 4090 tuning can be reused](RTX_4090_AUTOTUNE_CACHE.md)** — matching
  jobs can skip repeating the complete GPU tuning search.
- **[More password-candidate options](#3-more-ways-to-generate-password-candidates)**
  — join three or more wordlists, apply rules more broadly, and generate
  candidates faster.
- **[UTF-8 masks and rules work correctly on Windows](#4-better-windows-unicode-support)**
  — literal characters such as `№` are preserved.
- **[Long jobs are easier to control and recover](#5-reliability-and-clearer-status-information)**
  — coordinated checkpoints, retries, safer loopback files, and clearer
  shutdown progress.
- **[More hash-type compatibility](#additional-hash-types)** — the complete
  mdxfind `e1`-`e1001` namespace plus seven Shooter numeric or compatibility
  modes.
- **[One complete Windows download](#download-and-run)** — the `.7z` includes
  both a ready-to-run build and everything needed to rebuild it.

## Shooter compared with the original hashcat baseline

| Area | Original hashcat baseline | What Shooter adds |
| --- | --- | --- |
| Opening huge hash lists | Uses the normal general-purpose parsing and sorting paths. | Uses up to 64 CPU workers to parse compatible text hash lists and sort large unsalted lists. This directly reduces time spent at `Parsing Hashes`. |
| Starting many GPUs | Performs general backend discovery and GPU setup. | On the intended 12 x RTX 4090 system, skips redundant HIP/OpenCL discovery, initializes and shuts down GPUs concurrently, and commits much less host memory. |
| Repeating similar jobs | Performs the normal autotune search for each new matching session. | Saves successful RTX 4090 tuning settings, validates them, and reuses them across identical GPUs. |
| Creating password candidates | Provides the standard wordlist, mask, combination, and hybrid attacks. | Adds combinations of three or more wordlists, rules for the complete candidate in more attack modes, faster `--stdout` rule generation, and resumable candidate output. |
| Unicode on Windows | Some Windows command-line paths can lose non-ASCII literal characters. | Preserves UTF-8 literals in masks, rule files, and inline rules, including masks such as `?d?d№?d?d№?d?d№`. |
| Recovering from problems | Provides standard restore and session support. | Coordinates checkpoints across GPUs, retries temporary CUDA and outfile failures, protects loopback files, and shows progress during slow shutdowns. |
| Hash types | Provides the standard hashcat modes. | Adds the complete mdxfind `e1`-`e1001` namespace plus seven Shooter-specific or compatibility mode numbers. |
| Downloading and building | Normally requires obtaining the source or a suitable platform package. | Publishes one Windows `.7z` containing the complete tagged source, a ready-to-run build, verification tools, and a self-bootstrapping build script. |

## The main improvements

### 1. Very large hash lists start much faster

- Compatible text lists containing at least 4,194,304 hashes can be parsed in
  parallel with up to 64 CPU workers.
- Large unsalted lists can use a stable parallel radix sort.
- Unsupported or malformed inputs automatically fall back to hashcat's
  original path.
- On the development system, an 84,381,739-hash MD5 list improved from 33.56
  seconds to 6.41 seconds for parsing plus sorting. Total preprocessing through
  duplicate removal improved from 48.12 seconds to 11.28 seconds.

### 2. Multi-GPU jobs start and recover more cleanly

- CUDA contexts, memory checks, candidate-buffer setup, and GPU shutdown can
  run concurrently.
- The exact 12 x RTX 4090 setup automatically avoids duplicate backend probing
  and lowers host staging memory from about 97.7 GB to 36.7 GB in the measured
  fast-hash workload.
- RTX 4090 autotune results are saved and safely validated before reuse.
- A failed CUDA startup is retried as one complete session instead of silently
  continuing with only some GPUs.
- Checkpoints wait for all active GPUs at safe restore positions and can be
  cancelled without stranding an early GPU.

### 3. More ways to generate password candidates

- Combination attack mode 1 can join three or more wordlists:

  ```powershell
  hashcat.exe -m 0 -a 1 hashes.txt words1.txt words2.txt words3.txt
  ```

- Rules can transform the complete candidate in attack modes 1, 3, 6, and 7.
- High-volume wordlist-and-rule `--stdout` jobs can use up to 64 CPU workers
  while keeping deterministic output order.
- `--stdout` sessions support pause, checkpoint, quit, and exact file rollback
  when candidates are written with `-o`.
- The repository also includes newer official hashcat attack mode 12, which
  places one or two wordlist entries at chosen positions inside a mask.

### 4. Better Windows Unicode support

- Literal two-, three-, and four-byte UTF-8 characters work beside normal mask
  tokens in every mask-capable hash mode:

  ```powershell
  hashcat.exe -m 0 -a 3 hashes.txt "?d?d№?d?d№?d?d№"
  ```

- UTF-8 rule files can use literal characters with `$`, `^`, `i`, `v`, and
  `o` operations.
- UTF-8 BOM-prefixed rule files and Windows inline `-j`/`-k` rules are
  supported.

### 5. Reliability and clearer status information

- Temporary Windows outfile locks are retried instead of immediately losing
  output.
- `[k]eep-going` can disable `--outfile-check-dir` checking for the rest of a
  running session without changing files in that directory.
- A result found entirely through `--outfile-check-dir` is clearly reported as
  `cracked from outfile-check-dir`.
- Loopback induction files are released and removed safely instead of being
  rediscovered forever after a failed deletion.
- Interactive controls can extend or shorten a `--runtime` limit.
- Quit progress, total elapsed time, correct combinator filenames, and a yellow
  Pure Kernel warning make the current state easier to understand.
- Large recovered-result sets are written in batches and avoid unnecessary GPU
  round trips.

## Additional hash types

Shooter keeps every standard hashcat hash mode. Its additions are separate, so
existing numeric modes are not silently repurposed.

### mdxfind compatibility modes

Every entry in mdxfind's live registry is exposed as `e1` through `e1001`.
The complete name-by-name list is in
[docs/mdxfind-modules.json](docs/mdxfind-modules.json), with usage notes in
[docs/mdxfind-modules.md](docs/mdxfind-modules.md).

Of the 1,001 names, 999 are self-contained hash algorithms with passing test
vectors. `e426` is a scheduler pseudo-entry, and `e535` requires mdxfind's
external custom-user/salt state.

### Shooter numeric modes

| Mode | Purpose |
| --- | --- |
| `29950` | phpBB3 legacy `bcrypt(phpass($pass))` rehashes. |
| `29951` | Rare `bcrypt(phpass(md5($pass)))` variant. |
| `29960` | CMIYC 2026 SHA-512 GPU implementation. |
| `29970` | CMIYC 2026 memory-hard SHA-512 GPU implementation. |
| `29980` | Supported libxcrypt-style gost-yescrypt `$gy$j9T$` profile. |
| `29990` | Private CMIYC 2026 memory-hard SHA-512 mode. |
| `67000` | Compatibility number for legacy yescrypt jobs; use `36100` for new jobs. |

## Download and run

Download the single `windows-x64-complete.7z` asset from the
[latest release](https://github.com/Shooter3k/hashcat_shooter/releases/latest)
and extract it. The archive contains the complete tagged source and the
already-built Windows x64 program.

Verify the package and check the version:

```powershell
.\verify-windows-package.ps1
.\hashcat.exe --version
```

Rebuild everything from the included source:

```powershell
.\build-windows.ps1 -Action Rebuild
```

The first rebuild downloads a checksum-pinned MSYS2 compiler toolchain into
the local `.build-tools` directory. Allow internet access and at least 5 GB of
free disk space. Nothing is installed system-wide, and the system or user
`PATH` is not changed. GPU vendor drivers remain an external requirement.

## Which features require the 12-GPU system?

Only the automatic CUDA-only probe and the 3072 MiB-per-GPU staging limit
require the exact Windows 12 x RTX 4090 configuration. Persisted autotuning is
specific to RTX 4090 cards but does not require twelve of them.

Large-list parsing and sorting, candidate-generation features, Unicode
handling, checkpoint and reliability improvements, additional hash modes, and
release packaging are not limited to that exact machine unless their detailed
documentation says otherwise.

## Technical details

| Topic | Documentation |
| --- | --- |
| Startup, memory, parsing, and sorting | [docs/startup-optimization.md](docs/startup-optimization.md) |
| RTX 4090 autotune cache | [RTX_4090_AUTOTUNE_CACHE.md](RTX_4090_AUTOTUNE_CACHE.md) |
| Multi-file combinations and complete-candidate rules | [docs/multi-file-combination.md](docs/multi-file-combination.md) and [docs/whole-candidate-rules.md](docs/whole-candidate-rules.md) |
| Runtime and checkpoint controls | [docs/runtime-controls.md](docs/runtime-controls.md) and [docs/checkpoint-control.md](docs/checkpoint-control.md) |
| Resumable candidate output | [docs/stdout-sessions.md](docs/stdout-sessions.md) |
| mdxfind modes | [docs/mdxfind-modules.md](docs/mdxfind-modules.md) and the [complete JSON registry](docs/mdxfind-modules.json) |
| Windows builds | [how_to_compile.txt](how_to_compile.txt) |
| Every release and verification result | [CHANGELOG.md](CHANGELOG.md) |

## Comparison baseline and upstream work

The original Shooter fork is based on upstream hashcat commit
[`fdad9f2f7`](https://github.com/hashcat/hashcat/commit/fdad9f2f7bd7ec7f53056727e39331a17514db7c).
The complete source comparison is available in
[GitHub's compare view](https://github.com/Shooter3k/hashcat_shooter/compare/fdad9f2f7bd7ec7f53056727e39331a17514db7c...master).

Shooter was later synchronized with official hashcat through
[`9c735bade`](https://github.com/hashcat/hashcat/commit/9c735badebda0792b78010a5b94e3c8733bc1825).
Official attack mode 12 and the upstream correctness fixes in that range are
included but are not claimed as Shooter-authored work.

## Upstream hashcat and license

General hashcat help is available from the [Hashcat Wiki](https://hashcat.net/wiki/),
[`--help`](docs/hashcat-help.md), the
[Hashcat Forum](https://hashcat.net/forum/), and
[Discord](https://discord.gg/HFS523HGBT).

hashcat and the Shooter modifications are licensed under the MIT license. See
[docs/license.txt](docs/license.txt).
