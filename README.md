# hashcat_shooter

`hashcat_shooter` is a Windows-focused customization of hashcat 7.1.2 for a
12 x NVIDIA GeForce RTX 4090 system. It keeps the upstream hashcat feature set
and adds multi-GPU startup, tuning, checkpoint, runtime-control, reliability,
and custom-hash-mode work developed in the Shooter beta tree.

The current release is
[`v7.1.2-shooter.20260813.23`](https://github.com/Shooter3k/hashcat_shooter/releases/tag/v7.1.2-shooter.20260813.23).
Complete release-by-release notes are in [CHANGELOG.md](CHANGELOG.md).

> **Comparison baseline:** the first Shooter commit is based directly on
> upstream hashcat master commit
> [`fdad9f2f7`](https://github.com/hashcat/hashcat/commit/fdad9f2f7bd7ec7f53056727e39331a17514db7c)
> from August 10, 2026. The list below describes the custom behavior added
> after that exact commit. The complete source comparison is available in
> [GitHub's compare view](https://github.com/Shooter3k/hashcat_shooter/compare/fdad9f2f7bd7ec7f53056727e39331a17514db7c...master).

The source has now also been synchronized with official hashcat master through
[`9c735bade`](https://github.com/hashcat/hashcat/commit/9c735badebda0792b78010a5b94e3c8733bc1825)
from August 12, 2026. This includes the official general multi-hybrid attack
mode 12 from
[`554c1207a`](https://github.com/hashcat/hashcat/commit/554c1207ae367b551daf74ba641d0dc9fae419e0).

## Shooter changes from the upstream baseline

### Performance and 12-GPU startup

| Change | What it does |
| --- | --- |
| Automatic CUDA-only fast start | On Windows, normal cracking sessions automatically skip redundant HIP and OpenCL discovery when CUDA reports exactly twelve devices and every device is an `NVIDIA GeForce RTX 4090`. `-I`/`--backend-info` still probes every backend. Set `HASHCAT_SHOOTER_FAST_START=0` to disable this behavior. |
| Parallel CUDA initialization | Creates CUDA contexts and queries available memory concurrently across the GPUs instead of serializing the dominant WDDM startup work. |
| Parallel device teardown | Releases per-device session resources and CUDA contexts concurrently at shutdown. |
| Faster candidate staging | Initializes the large per-GPU host candidate buffers concurrently, avoids zero-filling data that is overwritten before use, and resets only required metadata between sessions. |
| Lower startup memory commitment | On the exact Windows 12 x RTX 4090 configuration, the two candidate-pipeline slots are limited to 3072 MiB per GPU by default. This reduced the tested fast-hash host allocation from about 97.7 GB to 36.7 GB. Set `HASHCAT_SHOOTER_HOST_STAGING_MB` to another MiB value, or to `0` for upstream's generic limit. |
| Parallel large-hashlist sorting | Unsalted lists containing at least 4,194,304 hashes use a stable parallel radix sort instead of the single-threaded comparison sort. It uses up to 64 CPU workers and preserves hashcat's exact digest ordering and duplicate-removal behavior. Smaller and salted lists retain the upstream sorter. |
| Persisted RTX 4090 autotune profiles | Saves successful accel, loops, threads, and timing selections in `hashcat.autotune-cache`; validates them with initialized test launches on later runs; and lets identical 4090s share one profile. This avoids repeating the full tuning search for matching jobs. |
| Multi-GPU-safe autotune logging | Serializes concurrent cache messages so output from twelve tuning threads retains the correct device number and does not become duplicated or interleaved. |
| Blowfish compiled-kernel cache | Removes upstream's forced JIT-cache disable for Blowfish-based modes `3200`, `25600`, `25800`, `28400`, `30600`, `30601`, `33800`, and `35500`, allowing their compiled kernels to be cached and reused. |

The RTX 4090 autotune cache is used by the shared GPU autotune path, not by
only one attack type. It covers standard GPU cracking attack modes, including
multi-file mode 1, and `--slow-candidates` when the complete cache key matches. It never
overrides explicit `-n`, `-u`, or `-T` settings and is disabled for bridges,
non-cracking `--stdout`, and custom modes 29960, 29970, and 29990. Set
`HASHCAT_AUTOTUNE_CACHE_DISABLE=1` to bypass it for a run.

See [docs/startup-optimization.md](docs/startup-optimization.md) and
[RTX_4090_AUTOTUNE_CACHE.md](RTX_4090_AUTOTUNE_CACHE.md) for the exact guards,
cache key, validation rules, overrides, and measurements.

### Official attack mode 12

Official attack mode 12 is a general multi-hybrid mode. Its mask is the first
attack argument, `?w` marks the first wordlist's position, and optional `?q`
marks a second wordlist after `?w`:

```powershell
hashcat.exe -m 0 -a 12 hashes.txt "?w-?d?d" words.txt
hashcat.exe -m 0 -a 12 hashes.txt "?w-?q!" left.txt right.txt
```

No-rule two-file `-a 1`, `-a 6`, and `-a 7` commands are internally mapped to
this maintained official implementation. Shooter's three-or-more-wordlist
`-a 1` extension remains on its custom path, and adding `-r` or `-g` keeps
modes 1, 3, 6, and 7 on Shooter's whole-candidate-rule paths. The old private
Shooter mode numbered 12 is not restored; official mode 12 has different
syntax and behavior. See
[docs/hashcat-generic-attack-mode.md](docs/hashcat-generic-attack-mode.md) for
the complete upstream syntax and restrictions.

### Multi-file mode 1 and whole-candidate rules

Rule support in this build is summarized below. Generic mode 8 also supports
rules, so it is marked accordingly rather than left unlabeled:

```text
  # | Mode
 ===+======
  0 | Straight + rules
  1 | Combination + rules
  3 | Brute-force + rules
  6 | Hybrid Wordlist + Mask + rules
  7 | Hybrid Mask + Wordlist + rules
  8 | Generic + rules
  9 | Association + rules
 12 | Hybrid, mask says where the word goes
```

Modes 0, 8, and 9 use their native rule processing. Shooter modes 1, 3, 6,
and 7 apply `-r` rule files or `-g` generated rules to the complete candidate.
Official mode 12 is not labeled with rules because its current path does not
provide the same rule option.

Attack mode 1 accepts two or more wordlists. Each candidate is one entry from
every wordlist concatenated in command-line order:

```powershell
hashcat.exe -m 0 -a 1 hashes.txt words1.txt words2.txt words3.txt words4.txt
```

The normal two-file mode keeps hashcat's optimized native combinator path. For
three or more files, the pipelined CPU producer concatenates the first `N - 1`
entries and the existing GPU combinator kernel supplies the final word. Work
ranges jump directly to their mixed-radix wordlist positions, so later GPUs do
not replay the ranges assigned to earlier GPUs. Progress, restore, status,
potfile, outfile, overflow, and password-length accounting use the normal
hashcat paths.

The former private Shooter modes 11-14 were removed; their fixed 3-6-file
layouts are now expressed with `-a 1` and the corresponding number of files.
The newly inherited official `-a 12` is the unrelated general multi-hybrid
mode described above. Full usage and examples are in
[docs/multi-file-combination.md](docs/multi-file-combination.md).

Ordinary `-r` rule files and `-g` generated rules can now be applied to the
complete candidate produced by modes 1, 3, 6, and 7. For example,
mode 1 applies the rule to `left + right`, mode 6 applies it to `word + mask`,
and multi-file mode 1 applies it after all words have been concatenated. Mode 8
retains its upstream native rule implementation; mode 9 keeps its native
association-rule support. Without `-r` or `-g`, the original optimized attack
path is unchanged. See
[docs/whole-candidate-rules.md](docs/whole-candidate-rules.md) for ordering,
examples, accounting, restore behavior, and compatibility limits.

For two-wordlist mode-1 attacks with whole-candidate rules, status names the
actual left and right files in `Guess.Base` and `Guess.Mod`. The `.15` fix
prevents the left filename from appearing as `File ((null))`; it does not alter
candidate generation or the native no-rule combinator path.

### Interactive runtime controls

Attacks started with a positive `--runtime` value gain two interactive keys:

- `[e]xtend` freezes the runtime countdown. Each second left enabled adds one
  second of permitted wall time.
- `[l]ower` makes the runtime countdown advance at 2x speed, reducing the
  remaining runtime faster.

The controls are mutually exclusive, can switch directly between each other,
and pause their adjustment while the cracking devices are paused. See
[docs/runtime-controls.md](docs/runtime-controls.md).

### Multi-GPU checkpoint behavior

Checkpoint requests now coordinate all active GPUs at a shared barrier. A GPU
that reaches a safe restore boundary waits with its worker and backend context
alive while the other GPUs arrive. Cancelling the checkpoint releases every
waiting GPU, including GPUs that reached the boundary first, and candidate
producers pause without discarding prefetched work. A completed checkpoint
still writes the normal restore file and exits as `Aborted (Checkpoint)`.

Skipped GPUs and devices that naturally finish at the end of the keyspace do
not hold the barrier open. See
[docs/checkpoint-control.md](docs/checkpoint-control.md).

### Resumable `--stdout` sessions

Mask- and file-driven `--stdout` candidate-generation sessions now use the
same interactive menu as cracking sessions. `[p]ause`, `[r]esume`,
`[c]heckpoint`, and `[q]uit` are available, and all menu/status text goes to
stderr so the candidate stream remains clean.

For exact continuation, direct candidates to a regular file with `-o` and use
a named session. Each restore point stores the committed candidate position
and the matching outfile byte boundary. On `--restore`, an uncommitted tail is
truncated before generation resumes, including on multi-GPU runs:

```powershell
hashcat.exe --stdout -a 3 "?d?d?d?d?d?d?d?d" `
  -o M:\candidates.txt --session=stdout-candidates

hashcat.exe --session=stdout-candidates --restore
```

Direct stdout and pipes can restore the candidate position but cannot retract
bytes already consumed downstream. Candidate input read from stdin also cannot
share stdin with the interactive menu. Full behavior and limitations are in
[docs/stdout-sessions.md](docs/stdout-sessions.md).

### Reliability and reporting

| Change | What it does |
| --- | --- |
| Interactive outfile-check bypass | When outfile-directory checking is active, the menu shows `[k]eep-going`. Pressing `k` stops checking `--outfile-check-dir` for the rest of the current run without changing anything in that directory. Hashes already processed remain marked; starting or restoring a process begins with checking enabled again. |
| Atomic CUDA startup retry | If CUDA context creation fails on any selected device, releases the complete partial attempt, waits 5 seconds, and retries the clean session up to 10 times. CUDA stream and event creation retry in place on the affected device with the same interval and limit. A multi-GPU job no longer silently continues on only the devices that initialized successfully. |
| Transient Windows outfile recovery | When `-o` is temporarily denied or locked, startup validation and result-time append opens retry every 250 ms for up to 5 seconds. After an exhausted window, a 30-second cooldown prevents repeated five-second stalls while later results still get an immediate open attempt. The successful path remains a single open with no retry delay. |
| Buffered stdout outfile recovery | The same outfile-open helper is used for normal recovered results and buffered `--stdout` output directed through `-o`. |
| Resumable stdout output | `--stdout -o` checkpoints bind the candidate position to an exact outfile byte boundary and roll back any partial tail before restore. Interactive controls and messages use stderr. |
| Visible quit progress | Pressing `q` or `Q` reports candidate-dispatch and GPU-kernel drain, GPU-worker completion, session-service shutdown, GPU-resource release, and final restore/session-file finalization instead of leaving the console apparently idle. |
| Accurate combinator status | Two-wordlist `-a 1` runs with whole-candidate `-r`/`-g` rules report the actual left and right wordlist paths instead of a `(null)` feed label. |
| Total elapsed time | The final summary now prints `Total Time` calculated from the displayed `Started` and `Stopped` timestamps. |
| Dated build identity | Production builds report `v7.1.2-shooter.YYYYMMDD.REVISION`, making the binary's source/release generation visible in `hashcat.exe --version`. |

### Added and compatibility hash modes

Every algorithm in mdxfind's live `Types[]` registry is also available under
its mdxfind name, from `e1` through `e1001`. For example:

```powershell
hashcat.exe -m e987 hashes.txt wordlist.txt
hashcat.exe -m e996 hashes.txt wordlist.txt
```

These are separate `module_eN` plugins; no existing numeric hashcat module is
changed. The generator reuses an existing hashcat implementation where
mdxfind publishes a compatible mapping and otherwise creates a front end for
the bundled Hashpipe verifier with an mdxfind expression-VM fallback. See
[docs/mdxfind-modules.md](docs/mdxfind-modules.md) for input fields, bridge
coverage, regeneration, and the complete machine-readable registry.

`e987` accepts both standard Argon2 PHC strings and mdxfind's Magento
`hex_digest:salt:2` / `hex_digest:salt:3_...` input forms.

| Hash mode | Addition |
| --- | --- |
| `29950` | phpBB3 legacy rehashes in their original `$H\2*$...` form: `bcrypt(phpass($pass))`. The parser extracts both embedded settings and the GPU runs the phpass and bcrypt stages without preprocessing the hash file. |
| `29951` | Explicit rare-case companion for `bcrypt(phpass(md5($pass)))`; it uses the same original and extracted input formats but never guesses which inner pipeline a hash uses. |
| `29960` | CMIYC 2026 SHA-512 GPU implementation with fixed-block optimizations and a PowerShell sharded launcher for small candidate sets on many GPUs. |
| `29970` | CMIYC 2026 memory-hard SHA-512 GPU implementation retained as a known-good implementation. |
| `29980` | GPU implementation of libxcrypt-style gost-yescrypt `$gy$j9T$` hashes for the supported default profile. |
| `29990` | Private CMIYC 2026 memory-hard SHA-512 mode carried forward from the beta tree. |
| `67000` | Compatibility alias for legacy yescrypt jobs. It accepts the same `$y$` hashes as current mode `36100` and shares the maintained mode 36100 implementation to avoid algorithm drift. |

Mode `36100` remains the preferred yescrypt number for new jobs. Technical
notes for the custom modes are in [docs/mode-29950.md](docs/mode-29950.md),
[CMIYC_GPU.md](CMIYC_GPU.md),
[CMIYC_GPU_OPTIMIZED.md](CMIYC_GPU_OPTIMIZED.md),
[GOST_YESCRYPT_GPU.md](GOST_YESCRYPT_GPU.md), and
[docs/mode-67000.md](docs/mode-67000.md).

### Windows build and maintenance material

- Added a reproducible MSYS2/MinGW64 production-build procedure in
  [how_to_compile.txt](how_to_compile.txt), including the required clean build
  after structure/header changes and Windows runtime troubleshooting.
- Added focused documentation for startup tuning, the autotune cache,
  checkpoints, runtime controls, multi-file mode 1, and mode 67000.
- Added release-by-release change and verification notes in
  [CHANGELOG.md](CHANGELOG.md).
- Added [CMIYC_SHARDED_LAUNCH.ps1](CMIYC_SHARDED_LAUNCH.ps1) to split a small
  CMIYC workload across independently running GPUs and safely combine results.
- Added generated `hashcat.autotune-cache` state to `.gitignore`.

## What was already upstream

This comparison intentionally does not claim upstream work as a Shooter
change. In particular, the starting commit already contained the upstream
attack-mode 9 update from
[`387cfdd`](https://github.com/hashcat/hashcat/commit/387cfdda3d3844c26bb96d2b04c1a1b21c9ec77f)
and upstream hash mode `17230`. Those capabilities remain present, but they are
inherited from hashcat rather than added by this branch.

All standard upstream attack modes and hash modes remain available. The
Shooter performance and reliability changes are implemented in shared paths
where applicable. The private hash modules and multi-file extension to mode 1
are additions, not replacements for standard modes.

## Verification on the target system

The published releases were clean-built with MSYS2/MinGW64 and exercised on
the intended Windows system with twelve RTX 4090 GPUs. Recorded verification
includes:

- Known-answer tests for standard attack modes 0, 1, 3, 6, 7, 8, and 9;
  mode 1 with two through eight files; whole-candidate rules; `--slow-candidates`; and
  custom/compatibility modes.
- A cold 12-GPU NTLM straight/rules run that saved one shared autotune profile,
  followed by two warm runs that reused it on all twelve devices with no cache
  rejection. The recorded speeds were 479.8, 479.2, and 479.1 GH/s for that
  specific short workload.
- Checkpoint enable/cancel testing that returned all twelve GPUs to fresh,
  nonzero speeds, plus completed checkpoint and `--restore` testing on all
  devices.
- Startup tests that reduced the short mode-0 case from approximately 18-23
  seconds to 15.8-16.9 seconds cold and 7.6-10.1 seconds warm while reducing
  host staging from approximately 97.7 GB to 36.7 GB.
- Startup-denied, runtime-exclusive-lock, persistent-lock, and unlocked-control
  outfile tests for the Windows retry path.
- GPU known-answer tests for both legacy yescrypt mode 67000 and current mode
  36100.
- GPU known-answer tests for phpBB3 rehash modes 29950 and 29951, including
  original and extracted inputs, different inner counts and bcrypt costs,
  long candidates, rules, masks, and all twelve CUDA devices.
- Exact two-wordlist `-a 1 -r` status reproduction using the reported input
  files, plus deterministic candidate-output checks for ruled two- and
  three-wordlist attacks and the unchanged native two-wordlist path.

These numbers describe the recorded hardware, driver state, and workloads;
they are not universal performance guarantees. The test-by-test evidence is
preserved in [CHANGELOG.md](CHANGELOG.md).

## Download, build, and run

Download the
[latest Shooter release](https://github.com/Shooter3k/hashcat_shooter/releases/latest)
or build it locally using [how_to_compile.txt](how_to_compile.txt). The normal
Windows production build is:

```powershell
$env:MSYSTEM='MINGW64'
& 'M:\msys64\usr\bin\bash.exe' -lc 'cd /m/github/hashcat_shooter && make PRODUCTION=1 -j'
```

Run the resulting executable from PowerShell with the MinGW64 runtime on
`PATH`:

```powershell
$env:PATH = "M:\msys64\mingw64\bin;$env:PATH"
M:\github\hashcat_shooter\hashcat.exe --version
```

Use this software only on passwords and systems you own or are explicitly
authorized to audit.

## Upstream hashcat

hashcat is an advanced password-recovery utility supporting CPUs, GPUs, and
other hardware accelerators on Linux, Windows, and macOS. General usage help
is available from the [Hashcat Wiki](https://hashcat.net/wiki/), `--help`, the
[FAQ](https://hashcat.net/wiki/doku.php?id=frequently_asked_questions), the
[Hashcat Forum](https://hashcat.net/forum/), and
[Discord](https://discord.gg/HFS523HGBT).

For generic upstream build instructions, see [BUILD.md](BUILD.md). Platform
package information is in [docs/packages.md](docs/packages.md).

## License

hashcat and these modifications are licensed under the MIT license. See
[docs/license.txt](docs/license.txt).

## Contributing

Changes should compile cleanly with the existing project warning settings and
follow hashcat's GNU99, Allman-style, two-space-indentation conventions. For
performance work, document the workload, hardware, before/after measurement,
and trade-offs so results can be reproduced.
