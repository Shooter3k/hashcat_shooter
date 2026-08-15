# hashcat_shooter

`hashcat_shooter` is a Windows-focused version of hashcat 7.1.2. It keeps the
normal hashcat commands and features, then adds faster handling of very large
hash lists, better multi-GPU behavior, more ways to build password candidates,
multibyte masks and rules on Windows, additional hash types, and a ready-to-run
release.

It was developed for a Windows machine with 12 NVIDIA GeForce RTX 4090 GPUs,
but most of its additions also work on other hardware.

Use this software only on passwords and systems you own or are explicitly
authorized to audit.

## Improvements at a glance

This is the complete user-visible feature inventory through release `.36`.
Items 14 and 43 came from newer official hashcat work included after the fork;
the other items are Shooter changes. Every item links to a plain-language
explanation.

1. **[Huge hash lists parse across CPU cores](docs/shooter-enhancements.md#1-parallel-hash-list-parsing)** — less time waiting at `Parsing Hashes`.
2. **[Huge unsalted hash lists sort across CPU cores](docs/shooter-enhancements.md#2-parallel-hash-list-sorting)** — preprocessing finishes sooner.
3. **[The 12-GPU system skips unnecessary backend scans](docs/shooter-enhancements.md#3-automatic-cuda-only-fast-start)** — short jobs begin faster.
4. **[Multiple GPUs start and stop concurrently](docs/shooter-enhancements.md#4-concurrent-gpu-setup-and-shutdown)** — devices no longer wait on one-at-a-time setup.
5. **[The 12-GPU system uses much less host memory](docs/shooter-enhancements.md#5-lower-host-memory-use)** — measured staging fell from about 97.7 GB to 36.7 GB.
6. **[RTX 4090 tuning is saved and reused](docs/shooter-enhancements.md#6-reusable-rtx-4090-autotuning)** — matching jobs can skip repeated tuning.
7. **[Blowfish kernels can be reused](docs/shooter-enhancements.md#7-blowfish-kernel-caching)** — supported Blowfish modes avoid needless recompilation.
8. **[Large cracked-result sets write much faster](docs/shooter-enhancements.md#8-high-volume-result-streaming)** — outfile and potfile results are batched safely.
9. **[Attack mode 1 joins three or more wordlists](docs/shooter-enhancements.md#9-multi-file-combination-attacks)** — no fixed three-to-six-file attack modes are needed.
10. **[Multi-GPU combination attacks seek directly to each GPU's work](docs/shooter-enhancements.md#10-efficient-multi-gpu-combination-starts)** — later GPUs do not replay earlier combinations.
11. **[Rules can modify the complete candidate](docs/shooter-enhancements.md#11-whole-candidate-rules)** — supported in attack modes 1, 3, 6, and 7.
12. **[`--stdout` rule generation uses multiple CPU cores](docs/shooter-enhancements.md#12-parallel-stdout-rule-generation)** — up to 64 workers retain deterministic order.
13. **[`--stdout` jobs can pause, checkpoint, and restore](docs/shooter-enhancements.md#13-resumable-stdout-sessions)** — file output resumes at the exact byte boundary.
14. **[Official attack mode 12 is included](docs/shooter-enhancements.md#14-official-attack-mode-12)** — place one or two wordlist entries anywhere inside a mask.
15. **[Multibyte masks work on Windows](docs/shooter-enhancements.md#15-multibyte-masks-work-on-windows)** — literals such as `№` survive the command line.
16. **[Multibyte rules work on Windows](docs/shooter-enhancements.md#16-multibyte-rules-work-on-windows)** — rule files and inline rules accept UTF-8 literals.
17. **[A running time limit can be extended or shortened](docs/shooter-enhancements.md#17-interactive-runtime-controls)** — adjust `--runtime` without restarting.
18. **[Multi-GPU checkpoints keep every GPU coordinated](docs/shooter-enhancements.md#18-coordinated-multi-gpu-checkpoints)** — checkpoint cancellation safely resumes all devices.
19. **[CUDA startup failures retry without dropping GPUs](docs/shooter-enhancements.md#19-atomic-cuda-startup-retry)** — a failed device cannot silently produce a partial-GPU run.
20. **[Temporarily locked output files are retried](docs/shooter-enhancements.md#20-windows-outfile-lock-recovery)** — brief Windows locks do not immediately lose results.
21. **[`[k]eep-going` can stop outfile-directory checking](docs/shooter-enhancements.md#21-interactive-outfile-check-bypass)** — disable it for the current run without changing the directory.
22. **[Outfile-check-only cracks are labeled clearly](docs/shooter-enhancements.md#22-clear-outfile-check-completion-status)** — the final status identifies where the result came from.
23. **[Loopback induction files clean up safely on Windows](docs/shooter-enhancements.md#23-reliable-loopback-cleanup)** — consumed files are not rediscovered forever.
24. **[Quit shows shutdown progress](docs/shooter-enhancements.md#24-visible-quit-progress)** — the console explains what hashcat is still finishing.
25. **[The final summary shows total elapsed time](docs/shooter-enhancements.md#25-total-elapsed-time)** — `Total Time` is calculated from start and stop timestamps.
26. **[Combination status shows the correct wordlist paths](docs/shooter-enhancements.md#26-correct-combination-status)** — ruled mode-1 jobs no longer show `(null)`.
27. **[Pure Kernel selection is highlighted](docs/shooter-enhancements.md#27-visible-pure-kernel-warning)** — interactive status displays the line in yellow.
28. **[The complete mdxfind `e1`-`e1001` namespace is available](docs/shooter-enhancements.md#28-complete-mdxfind-namespace)** — 999 standalone algorithms plus two documented special entries.
29. **[mdxfind names appear consistently throughout hashcat](docs/shooter-enhancements.md#29-public-mdxfind-mode-names)** — help, status, benchmarks, and logs show public `eN` names.
30. **[mdxfind `e987` accepts Magento Argon2 input](docs/shooter-enhancements.md#30-magento-argon2-input)** — original Magento lines remain intact in output and potfiles.
31. **[Modes 29950 and 29951 handle phpBB3 legacy rehashes](docs/shooter-enhancements.md#31-phpbb3-bcrypt-over-phpass-modes)** — the complete two-stage hashes run on the GPU.
32. **[Mode 29960 adds CMIYC 2026 SHA-512](docs/shooter-enhancements.md#32-mode-29960)** — a custom GPU implementation.
33. **[Mode 29970 adds CMIYC 2026 memory-hard SHA-512](docs/shooter-enhancements.md#33-mode-29970)** — a retained known-good GPU implementation.
34. **[Mode 29980 adds the supported gost-yescrypt profile](docs/shooter-enhancements.md#34-mode-29980)** — handles libxcrypt-style `$gy$j9T$` hashes.
35. **[Mode 29990 adds the private CMIYC 2026 mode](docs/shooter-enhancements.md#35-mode-29990)** — a retained memory-hard SHA-512 GPU implementation.
36. **[Mode 67000 restores legacy yescrypt numbering](docs/shooter-enhancements.md#36-mode-67000)** — old jobs use the maintained mode-36100 implementation.
37. **[One `.7z` contains both source and a ready-to-run build](docs/shooter-enhancements.md#37-complete-windows-release-archive)** — download one file to run or rebuild Shooter.
38. **[Release contents can be verified locally](docs/shooter-enhancements.md#38-package-integrity-verification)** — an included script checks the complete SHA-256 manifest.
39. **[Windows builds bootstrap with one command](docs/shooter-enhancements.md#39-self-bootstrapping-windows-build)** — the compiler toolchain stays inside the repository.
40. **[Fresh clones build from any drive](docs/shooter-enhancements.md#40-portable-windows-build-instructions)** — no machine-specific `M:\` paths are required.
41. **[Release versions are reproducible](docs/shooter-enhancements.md#41-reproducible-release-versioning)** — tags, packages, rebuilds, and `--version` stay identical.
42. **[CMIYC workloads can be split across GPUs](docs/shooter-enhancements.md#42-cmiyc-multi-gpu-sharding)** — the included launcher creates and combines independent shards.
43. **[Newer official hashcat fixes are included](docs/shooter-enhancements.md#43-newer-upstream-correctness-fixes)** — upstream fixes after the fork are preserved and clearly credited.

## Shooter compared with the original hashcat baseline

| Area | Original hashcat baseline | What Shooter adds |
| --- | --- | --- |
| Opening huge hash lists | Uses the normal general-purpose parsing and sorting paths. | Uses up to 64 CPU workers to parse compatible text hash lists and sort large unsalted lists. This directly reduces time spent at `Parsing Hashes`. |
| Starting many GPUs | Performs general backend discovery and GPU setup. | On the intended 12 x RTX 4090 system, skips redundant HIP/OpenCL discovery, initializes and shuts down GPUs concurrently, and commits much less host memory. |
| Repeating similar jobs | Performs the normal autotune search for each new matching session. | Saves successful RTX 4090 tuning settings, validates them, and reuses them across identical GPUs. |
| Creating password candidates | Provides the standard wordlist, mask, combination, and hybrid attacks. | Adds combinations of three or more wordlists, rules for the complete candidate in more attack modes, faster `--stdout` rule generation, and resumable candidate output. |
| Multibyte masks and rules | Some Windows command-line paths can lose non-ASCII literal characters. | Preserves multibyte literals in masks, rule files, and inline rules, including masks such as `?d?d№?d?d№?d?d№`. |
| Recovering from problems | Provides standard restore and session support. | Coordinates checkpoints across GPUs, retries temporary CUDA and outfile failures, protects loopback files, and shows progress during slow shutdowns. |
| Hash types | Provides the standard hashcat modes. | Adds the complete mdxfind `e1`-`e1001` namespace plus seven Shooter-specific or compatibility mode numbers. |
| Downloading and building | Normally requires obtaining the source or a suitable platform package. | Publishes one Windows `.7z` containing the complete tagged source, a ready-to-run build, verification tools, and a self-bootstrapping build script. |

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

Large-list parsing and sorting, candidate-generation features, multibyte mask
and rule handling, checkpoint and reliability improvements, additional hash
modes, and release packaging are not limited to that exact machine unless
their detailed documentation says otherwise.

## Technical details

| Topic | Documentation |
| --- | --- |
| Complete Shooter feature inventory | [docs/shooter-enhancements.md](docs/shooter-enhancements.md) |
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
