# hashcat_shooter

`hashcat_shooter` is a Windows-focused edition of hashcat 7.1.2 built for
large password-recovery jobs, especially on a 12 x NVIDIA GeForce RTX 4090
system. It keeps the standard hashcat commands and capabilities, then adds
faster preparation of very large hash lists, multi-GPU startup and recovery
improvements, more candidate-generation options, Windows Unicode support,
extra hash formats, and a ready-to-run Windows release.

## Start here

If you already use hashcat, Shooter is intended to be a drop-in replacement:
the normal attack modes, hash modes, potfiles, restore files, rules, masks,
and command-line options remain available. The additions are automatic where
they are safe, narrowly guarded where they are hardware-specific, and usually
have an environment-variable opt-out for comparison testing.

If you are new to hashcat, the practical differences are:

| Area | What Shooter adds | Why it matters |
| --- | --- | --- |
| Starting a 12-GPU job | Detects the intended all-NVIDIA system, avoids duplicate GPU-backend discovery, initializes GPUs concurrently, and prepares less host memory. | Short jobs begin sooner, shutdown is faster, and Windows commits much less RAM before cracking starts. |
| Opening huge hash lists | Parses compatible text hash files and sorts large unsalted lists with up to 64 CPU workers. | Files containing millions of hashes spend much less time at `Parsing Hashes` and in preprocessing before the GPUs can work. |
| Repeating similar jobs | Saves and validates successful RTX 4090 autotune settings and reuses them across identical cards. | Later runs can avoid repeating the full GPU tuning search. |
| Building password candidates | Extends combination mode to three or more wordlists and lets rules transform the complete result of combination, mask, and hybrid attacks. | More candidate strategies can run directly in hashcat instead of requiring large intermediate wordlists. |
| Generating wordlists | Parallelizes CPU rule processing for high-volume `--stdout` jobs and avoids unnecessary GPU transfers. | Generated candidates can be written or piped faster while retaining deterministic output order. |
| Unicode on Windows | Preserves UTF-8 literals in masks, rule files, and inline rules, including masks such as `?d?d№?d?d№?d?d№`. | Non-ASCII characters reach hashcat intact instead of being replaced or misread by the Windows command-line path. |
| Long-running jobs | Lets you adjust a runtime limit interactively, coordinates checkpoints across GPUs, makes `--stdout` generation resumable, retries transient CUDA/outfile failures, and reports shutdown progress. | Large jobs are easier to control, recover, and diagnose without silently losing devices or output. |
| Hash formats | Adds the mdxfind `e1`-`e1001` namespace plus Shooter-specific and compatibility modes. | Existing mdxfind workflows and several uncommon formats can use the same GPU-oriented tool. |
| Downloading and rebuilding | Publishes one Windows `.7z` containing the complete tagged source and a ready-to-run build, plus verification and self-bootstrapping build scripts. | A user can run the included executable immediately or rebuild the same source without assembling the repository by hand. |

The current release is
[`v7.1.2-shooter.20260814.36`](https://github.com/Shooter3k/hashcat_shooter/releases/tag/v7.1.2-shooter.20260814.36).
Download its single `windows-x64-complete.7z` asset, extract it, and run
`hashcat.exe`. See [Download, build, and run](#download-build-and-run) for the
verification and rebuild commands. Complete release-by-release notes are in
[CHANGELOG.md](CHANGELOG.md).

### A few hashcat terms

- A **hash list** is the input file containing the password hashes being
  audited.
- A **candidate** is one possible password that hashcat tests.
- A **wordlist** supplies candidates from a file; a **mask** describes a
  pattern such as digits or literal characters; a **rule** transforms a
  candidate before it is tested.
- A **hash mode** tells hashcat which password-hash format it must interpret.
- **Autotune** selects GPU workload settings. A **checkpoint** records a safe
  position from which a session can later continue.
- A **potfile** remembers hashes already recovered. An **outfile** is the
  result file explicitly selected with `-o`.

Only the automatic CUDA-only probe and 3072 MiB staging limit are tied to the
exact 12 x RTX 4090 setup. Persisted autotuning is specific to RTX 4090 cards
but does not require twelve of them. Large-list parsing and sorting,
candidate-generation features, Unicode handling, checkpoint/reliability
work, additional hash modes, and release packaging are not limited to that
exact machine unless a detailed section says otherwise.

> **Comparison baseline:** the first Shooter commit is based directly on
> upstream hashcat master commit
> [`fdad9f2f7`](https://github.com/hashcat/hashcat/commit/fdad9f2f7bd7ec7f53056727e39331a17514db7c)
> from August 10, 2026. The list below describes behavior that differs from
> that exact commit and labels whether it came from Shooter or newer upstream
> hashcat. The complete source comparison is available in
> [GitHub's compare view](https://github.com/Shooter3k/hashcat_shooter/compare/fdad9f2f7bd7ec7f53056727e39331a17514db7c...master).

The source has also been synchronized with official hashcat master through
[`9c735bade`](https://github.com/hashcat/hashcat/commit/9c735badebda0792b78010a5b94e3c8733bc1825)
from August 12, 2026. The official additions received after the fork are
listed separately below.

## What changed from the upstream baseline

Most sections below describe Shooter-specific work. One clearly labeled
section describes newer official hashcat work synchronized after the fork.
This distinction keeps the comparison complete without claiming upstream
features as Shooter inventions.

### Faster startup and very large jobs

| Enhancement | What it means in practice | When it applies |
| --- | --- | --- |
| Automatic CUDA-only fast start | Shooter skips redundant HIP and OpenCL discovery when it confirms the intended all-NVIDIA system. Diagnostic `-I`/`--backend-info` still probes everything. | Windows with exactly 12 detected RTX 4090 GPUs. Set `HASHCAT_SHOOTER_FAST_START=0` to disable it. |
| Parallel GPU setup and shutdown | CUDA contexts, memory queries, candidate-buffer preparation, and device teardown run concurrently instead of making twelve cards wait on one another. | Multi-GPU CUDA sessions. |
| Lower startup memory use | Candidate staging is capped at 3072 MiB per GPU across two pipeline slots. The measured fast-hash host commitment fell from about 97.7 GB to 36.7 GB. | The exact Windows 12 x RTX 4090 configuration. Change `HASHCAT_SHOOTER_HOST_STAGING_MB`, or set it to `0` for hashcat's generic limit. |
| Parallel hash-list parsing | Compatible native-text files with at least 4,194,304 nonempty hashes are memory-mapped and decoded with up to 64 CPU workers. Malformed or unsupported inputs automatically return to the original parser. | Raw, salted, structured, extended-salt, and other compatible text modes. Set `HASHCAT_HASH_PARSE_PARALLEL_DISABLE=1` to compare. |
| Parallel large-list sorting | Large unsalted lists use a stable parallel radix sort while preserving hashcat's digest order and duplicate removal. | Unsalted lists with at least 4,194,304 hashes; smaller or salted lists keep the original sorter. |
| Batched recovered-result output | Recovered plaintexts are rebuilt from retained host data instead of requiring a GPU round trip, then large result groups are written to the outfile and potfile in bounded batches. | Jobs recovering many hashes. |
| Reusable RTX 4090 autotune profiles | Successful GPU workload settings are saved, validated on later runs, and shared by identical cards. Log messages remain correctly associated with each GPU. | Matching RTX 4090 cracking jobs without explicit tuning values. Set `HASHCAT_AUTOTUNE_CACHE_DISABLE=1` to bypass it. |
| Blowfish kernel caching | Compiled kernels can be reused for Blowfish-based modes that upstream normally recompiles. | Modes `3200`, `25600`, `25800`, `28400`, `30600`, `30601`, `33800`, and `35500`. |

The RTX 4090 autotune cache is used by the shared GPU autotune path, not by
only one attack type. It covers standard GPU cracking attack modes, including
multi-file mode 1, and `--slow-candidates` when the complete cache key matches. It never
overrides explicit `-n`, `-u`, or `-T` settings and is disabled for bridges,
non-cracking `--stdout`, and custom modes 29960, 29970, and 29990. Set
`HASHCAT_AUTOTUNE_CACHE_DISABLE=1` to bypass it for a run.

See [docs/startup-optimization.md](docs/startup-optimization.md) and
[RTX_4090_AUTOTUNE_CACHE.md](RTX_4090_AUTOTUNE_CACHE.md) for the exact guards,
cache key, validation rules, overrides, and measurements.

### Newer official hashcat improvements included after the fork

These changes were authored upstream and synchronized into Shooter. They are
differences from the original baseline, but they are not claimed as
Shooter-authored work.

| Official improvement | Plain-language effect |
| --- | --- |
| General multi-hybrid attack mode 12 ([`554c1207a`](https://github.com/hashcat/hashcat/commit/554c1207ae367b551daf74ba641d0dc9fae419e0)) | Places one or two wordlist entries at chosen positions inside a mask. |
| Large-buffer and rule-processing safety fixes ([`62dcbb453`](https://github.com/hashcat/hashcat/commit/62dcbb453), [`8640be01c`](https://github.com/hashcat/hashcat/commit/8640be01c), [`e09c9650b`](https://github.com/hashcat/hashcat/commit/e09c9650b), [`fc6c49337`](https://github.com/hashcat/hashcat/commit/fc6c49337)) | Prevents integer-overflow, out-of-bounds-write, and double-free failures in hash/salt buffers, debug data, and chained rules. |
| Correct long combinator results and status ([`72f3700a6`](https://github.com/hashcat/hashcat/commit/72f3700a6), [`f457206e4`](https://github.com/hashcat/hashcat/commit/f457206e4)) | Stops long combined plaintexts from being truncated or displayed as a non-matching result. |
| PDF mode 10500 empty-ID support ([`0e3ae7e11`](https://github.com/hashcat/hashcat/commit/0e3ae7e11)) | Accepts valid PDF hashes whose ID field is empty. |
| Per-device feed routing ([`fc3a57ead`](https://github.com/hashcat/hashcat/commit/fc3a57ead)) | Tells each external candidate feed which GPU device it serves. |
| Yescrypt maintenance ([`5d5990ce3`](https://github.com/hashcat/hashcat/commit/5d5990ce3), [`9c735bade`](https://github.com/hashcat/hashcat/commit/9c735badebda0792b78010a5b94e3c8733bc1825)) | Fixes Metal address-space handling and reorganizes yescrypt around the maintained scrypt layout. |

#### Attack mode 12 example

A hybrid attack combines wordlist entries with a pattern. Official attack
mode 12 is a general multi-hybrid mode: its mask is the first attack argument,
`?w` marks the first wordlist's position, and optional `?q` marks a second
wordlist:

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

### Unicode masks and rules on Windows

Standard mask tokens such as `?d` still have their usual meaning, while
literal two-, three-, and four-byte UTF-8 characters are preserved beside
them. This works in every mask-capable hash mode. For example, this mask tests
six digits separated into pairs by the literal numero sign:

```powershell
hashcat.exe -m 0 -a 3 hashes.txt "?d?d№?d?d№?d?d№"
```

UTF-8 rule files can use literal multibyte characters with the byte-emitting
`$`, `^`, `i`, `v`, and `o` functions. Shooter compiles those literals into
the equivalent per-byte instructions for both host and GPU rule paths, and
accepts an optional UTF-8 BOM at the beginning of a rule file. Positions and
other transformations remain byte-oriented. Inline `-j` and `-k` literals
work on Windows too because Shooter converts the native wide command line to
UTF-8 before option parsing. See [docs/rules.txt](docs/rules.txt) for examples
and the exact scope. A ready-to-run set of 26 two-, three-, and four-byte test
rules is included in [multibyte-test.rule](multibyte-test.rule).

### Multi-file mode 1 and whole-candidate rules

Combination mode normally joins entries from two wordlists. Shooter can join
three or more wordlists in one attack, and its rule support can transform the
complete assembled candidate rather than only one piece of it. This avoids
pre-generating and storing very large intermediate wordlists.

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

`--runtime` normally gives a job a fixed time limit. Shooter lets an operator
adjust that limit without restarting the attack. A positive `--runtime` value
enables two interactive keys:

- `[e]xtend` freezes the runtime countdown. Each second left enabled adds one
  second of permitted wall time.
- `[l]ower` makes the runtime countdown advance at 2x speed, reducing the
  remaining runtime faster.

The controls are mutually exclusive, can switch directly between each other,
and pause their adjustment while the cracking devices are paused. See
[docs/runtime-controls.md](docs/runtime-controls.md).

### Multi-GPU checkpoint behavior

In standard multi-GPU work, different cards can reach safe stopping positions
at different times. Shooter checkpoint requests coordinate all active GPUs at
a shared barrier. A GPU that reaches a safe restore boundary waits with its
worker and backend context alive while the other GPUs arrive. Cancelling the
checkpoint releases every
waiting GPU, including GPUs that reached the boundary first, and candidate
producers pause without discarding prefetched work. A completed checkpoint
still writes the normal restore file and exits as `Aborted (Checkpoint)`.

Skipped GPUs and devices that naturally finish at the end of the keyspace do
not hold the barrier open. See
[docs/checkpoint-control.md](docs/checkpoint-control.md).

### Faster `--stdout` candidate generation

Straight wordlist-and-rule `--stdout` jobs apply rules in a reusable CPU
worker pool, write larger ordered buffers, and skip device uploads and
downloads that do not contribute to candidate generation. Up to 64 workers
can be used while preserving the same deterministic candidate order.

The selected workload profile controls the automatic worker count. Set
`HASHCAT_STDOUT_THREADS` to a positive number for a controlled comparison or
to limit CPU use. Pause, checkpoint, and quit controls remain responsive
between bounded output writes.

### Resumable `--stdout` sessions

`--stdout` generates candidates without hashing them, often for piping into
another program or saving a generated wordlist. Mask- and file-driven
`--stdout` sessions now use the same interactive menu as cracking sessions.
`[p]ause`, `[r]esume`,
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

These changes focus on protecting long-running work and making the program's
state visible when Windows, a GPU, or an output file temporarily misbehaves.

| Change | What it does |
| --- | --- |
| Interactive outfile-check bypass | When outfile-directory checking is active, the menu shows `[k]eep-going`. Pressing `k` stops checking `--outfile-check-dir` for the rest of the current run without changing anything in that directory. Hashes already processed remain marked; starting or restoring a process begins with checking enabled again. |
| Clear outfile-check completion status | If every recovered hash came from `--outfile-check-dir` rather than the current attack, the final status says `cracked from outfile-check-dir` so the source of the result is unambiguous. |
| Atomic CUDA startup retry | If CUDA context creation fails on any selected device, releases the complete partial attempt, waits 5 seconds, and retries the clean session up to 10 times. CUDA stream and event creation retry in place on the affected device with the same interval and limit. A multi-GPU job no longer silently continues on only the devices that initialized successfully. |
| Transient Windows outfile recovery | When `-o` is temporarily denied or locked, startup validation and result-time append opens retry every 250 ms for up to 5 seconds. After an exhausted window, a 30-second cooldown prevents repeated five-second stalls while later results still get an immediate open attempt. The successful path remains a single open with no retry delay. |
| Buffered stdout outfile recovery | The same outfile-open helper is used for normal recovered results and buffered `--stdout` output directed through `-o`. |
| Resumable stdout output | `--stdout -o` checkpoints bind the candidate position to an exact outfile byte boundary and roll back any partial tail before restore. Interactive controls and messages use stderr. |
| Reliable Windows loopback induction | Per-round wordlist feeds release their mappings before consumed loopback files are deleted. A deletion failure is reported and stops the run instead of silently rediscovering the same induction dictionary forever; successful and fully cracked runs remove their consumed loopback files, while abort and quit states preserve the active file for recovery. |
| Visible quit progress | Pressing `q` or `Q` reports candidate-dispatch and GPU-kernel drain, GPU-worker completion, session-service shutdown, GPU-resource release, and final restore/session-file finalization instead of leaving the console apparently idle. |
| Accurate combinator status | Two-wordlist `-a 1` runs with whole-candidate `-r`/`-g` rules report the actual left and right wordlist paths instead of a `(null)` feed label. |
| Total elapsed time | The final summary now prints `Total Time` calculated from the displayed `Started` and `Stopped` timestamps. |
| Visible Pure Kernel selection | Interactive terminals display the `Kernel.Feature...: Pure Kernel` status line in bright yellow, making an unoptimized kernel choice immediately visible. Optimized-kernel lines retain the normal color, and redirected or machine-consumed output remains plain text without ANSI escape codes. |
| Dated build identity | Production builds report `v7.1.2-shooter.YYYYMMDD.REVISION`, making the binary's source/release generation visible in `hashcat.exe --version`. |

### More hash formats and mdxfind compatibility

A hash mode is the format selector supplied with `-m`. Shooter keeps all
standard hashcat modes and adds separate plugins; existing numeric modes are
not repurposed or silently changed.

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

The complete validation run exercised every self-contained hash algorithm in
the registry on CUDA. All 988 example hashes published in Hashpipe's
`HASH_TYPES.md` cracked with their documented passwords, and 11 direct
known-answer tests covered the standalone modes omitted from that document.
The result is 999/999 passing standalone hash modes. The other two registry
names are special cases: `e426` (`PARALLEL`) is a scheduler pseudo-entry, not
a hash algorithm, and `e535` (`SHA1-CUSTOMUSERSALT`) requires mdxfind's
external custom-user/salt state and has no published standalone vector.

| Hash mode | Format or purpose | Practical note |
| --- | --- | --- |
| `29950` | phpBB3 legacy `bcrypt(phpass($pass))` rehashes in their original `$H\2*$...` form. | Hash files do not need to be preprocessed; the parser extracts both settings and the GPU performs both stages. |
| `29951` | Rare `bcrypt(phpass(md5($pass)))` variant. | Kept explicit so hashcat never guesses which inner password pipeline a hash uses. |
| `29960` | CMIYC 2026 SHA-512. | Includes GPU fixed-block optimizations and a PowerShell launcher that can shard small candidate sets across many GPUs. |
| `29970` | CMIYC 2026 memory-hard SHA-512. | Retained as a known-good GPU implementation. |
| `29980` | libxcrypt-style gost-yescrypt `$gy$j9T$`. | GPU implementation for the supported default profile. |
| `29990` | Private CMIYC 2026 memory-hard SHA-512. | Carried forward from the Shooter beta tree. |
| `67000` | Legacy yescrypt compatibility number. | Accepts the same `$y$` hashes as current mode `36100` and shares that maintained implementation. Use `36100` for new jobs. |

Mode `36100` remains the preferred yescrypt number for new jobs. Technical
notes for the custom modes are in [docs/mode-29950.md](docs/mode-29950.md),
[CMIYC_GPU.md](CMIYC_GPU.md),
[CMIYC_GPU_OPTIMIZED.md](CMIYC_GPU_OPTIMIZED.md),
[GOST_YESCRYPT_GPU.md](GOST_YESCRYPT_GPU.md), and
[docs/mode-67000.md](docs/mode-67000.md).

### Ready-to-run releases and Windows builds

- Each release publishes one `windows-x64-complete.7z` containing the complete
  tagged source, `hashcat.exe`, all built module/bridge/feed DLLs, required
  MinGW runtime DLLs, build metadata, and an internal `SHA256SUMS` manifest.
- The included `verify-windows-package.ps1` checks every packaged file. The
  included `build-windows.ps1` downloads a checksum-pinned, repository-local
  toolchain and rebuilds the source without changing the system `PATH`.
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

## Compatibility and feature origin

Shooter is an extension of hashcat, not a replacement for its normal feature
set. All standard upstream attack modes and hash modes remain available. The
performance and reliability changes use shared paths where appropriate, while
private hash modules and the multi-file mode-1 extension are additions.

This comparison intentionally does not claim upstream work as Shooter work.
In particular, the starting commit already contained the upstream
attack-mode 9 update from
[`387cfdd`](https://github.com/hashcat/hashcat/commit/387cfdda3d3844c26bb96d2b04c1a1b21c9ec77f)
and upstream hash mode `17230`. Those capabilities remain present, but they are
inherited from hashcat rather than added by this branch.

Official attack mode 12 was added to upstream after the original fork point
and then synchronized into this repository. It differs from the baseline but
is clearly identified above as newer upstream work.

## Verification on the target system

The enhancements above have been exercised rather than only compiled. The
published releases were clean-built with MSYS2/MinGW64 and tested on the
intended Windows system with twelve RTX 4090 GPUs. Recorded verification
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
- UTF-8 literal rule tests covering two-, three-, and four-byte operands on
  host and GPU rule paths, BOM-prefixed rule files, Windows inline `-j`/`-k`,
  legacy `\xNN` syntax, and Windows wildcard argument compatibility.
- Bounded ASCII and UTF-8 loopback cascades that each recovered three
  generations, exited normally, and left no consumed induction files behind.

These numbers describe the recorded hardware, driver state, and workloads;
they are not universal performance guarantees. The test-by-test evidence is
preserved in [CHANGELOG.md](CHANGELOG.md).

## Download, build, and run

Download the
[latest Shooter release](https://github.com/Shooter3k/hashcat_shooter/releases/latest)
and extract its single `windows-x64-complete.7z` asset. It contains both the
complete tagged source and a ready-to-run Windows x64 build. Run it directly
from the extracted directory:

```powershell
.\verify-windows-package.ps1
.\hashcat.exe --version
```

The internal `SHA256SUMS` manifest covers every packaged source and binary
file. `BUILD-INFO.txt` records the version, source commit, build counts, and
exact rebuild command.

To rebuild the included source, use the self-bootstrapping build wrapper:

```powershell
.\build-windows.ps1 -Action Rebuild
```

The first rebuild downloads a checksum-pinned official MSYS2 base and the
required compiler packages into `.build-tools`. Allow internet access and at
least 5 GB of free disk space. Nothing is installed system-wide and the
machine or user `PATH` is not changed. GPU vendor drivers remain an external
runtime prerequisite.

To create the same complete release archive from a clean clone:

```powershell
.\package-windows.ps1 -BuildAction Rebuild
```

The packager exports only committed source files, overlays the complete
Windows build, verifies that every source module has a matching DLL, writes
and checks the internal manifest, and tests the resulting `.7z`. Its default
output directory is `dist`.

Alternatively, clone and build locally:

```powershell
git clone https://github.com/Shooter3k/hashcat_shooter.git
cd hashcat_shooter
.\build-windows.ps1
```

If local script execution is restricted, use a process-only bypass (it does
not change the machine or user execution policy):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-windows.ps1
```

The toolchain cache is ignored by Git and can be deleted after the build. The
wrapper copies the required runtime DLLs beside the executable and checks its
version.

The traditional system-wide MSYS2 MINGW64 workflow remains supported. See
[how_to_compile.txt](how_to_compile.txt) for both approaches, dependency
details, cache location, and clean-build warnings.

The resulting `hashcat.exe` can be run directly from the repository because
its MinGW runtime DLLs are copied beside it.

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
