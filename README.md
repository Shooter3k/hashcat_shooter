# shooter_hashcat

`shooter_hashcat` is a Windows-focused version of hashcat 7.1.2. It keeps the
normal hashcat commands and features, then adds faster handling of very large
hash and rule lists, better multi-GPU behavior, more ways to build password
candidates, multibyte masks and rules on Windows, additional hash types, and a
ready-to-run release. If an error occurs, it also creates one support file that
can be reviewed and sent with a bug report.

It was developed for a Windows machine with 12 NVIDIA GeForce RTX 4090 GPUs,
but most of its additions also work on other hardware.

Use this software only on passwords and systems you own or are explicitly
authorized to audit.

## What shooter_hashcat can do that the original Hashcat baseline cannot

This is the public feature inventory for the current branch.
Items 14 and 39 came from newer official hashcat work included after the fork;
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
21. **[`[i]gnore outfile-check-dir` stops directory checking](docs/shooter-enhancements.md#21-ignore-outfile-check-dir)** — stop checking it for the current run without changing the directory.
22. **[Outfile-check-only cracks are labeled clearly](docs/shooter-enhancements.md#22-clear-outfile-check-completion-status)** — the final status identifies where the result came from.
23. **[Loopback induction files clean up safely on Windows](docs/shooter-enhancements.md#23-reliable-loopback-cleanup)** — consumed files are not rediscovered forever.
24. **[Quit shows shutdown progress](docs/shooter-enhancements.md#24-visible-quit-progress)** — the console explains what hashcat is still finishing.
25. **[The final summary shows total run time](docs/shooter-enhancements.md#25-total-run-time)** — `Total Run Time` is calculated from start and stop timestamps.
26. **[Combination status shows the correct wordlist paths](docs/shooter-enhancements.md#26-correct-combination-status)** — ruled mode-1 jobs no longer show `(null)`.
27. **[Pure Kernel selection is highlighted](docs/shooter-enhancements.md#27-visible-pure-kernel-warning)** — interactive status displays the line in yellow.
28. **[The complete mdxfind `e1`-`e1001` namespace is available](docs/shooter-enhancements.md#28-complete-mdxfind-namespace)** — 999 standalone algorithms plus two documented special entries.
29. **[mdxfind names appear consistently throughout hashcat](docs/shooter-enhancements.md#29-public-mdxfind-mode-names)** — help, status, benchmarks, and logs show public `eN` names.
30. **[mdxfind `e987` accepts Magento Argon2 input](docs/shooter-enhancements.md#30-magento-argon2-input)** — original Magento lines remain intact in output and potfiles.
31. **[Modes 29950 and 29951 handle phpBB3 legacy rehashes](docs/shooter-enhancements.md#31-phpbb3-bcrypt-over-phpass-modes)** — the complete two-stage hashes run on the GPU.
32. **[Mode 29980 adds the supported gost-yescrypt profile](docs/shooter-enhancements.md#32-mode-29980)** — handles libxcrypt-style `$gy$j9T$` hashes.
33. **[Mode 67000 restores legacy yescrypt numbering](docs/shooter-enhancements.md#33-mode-67000)** — old jobs use the maintained mode-36100 implementation.
34. **[One `.7z` contains both source and a ready-to-run build](docs/shooter-enhancements.md#34-complete-windows-release-archive)** — download one file to run or rebuild Shooter.
35. **[Release contents can be verified locally](docs/shooter-enhancements.md#35-package-integrity-verification)** — an included script checks the complete SHA-256 manifest.
36. **[Windows builds bootstrap with one command](docs/shooter-enhancements.md#36-self-bootstrapping-windows-build)** — the compiler toolchain stays inside the repository.
37. **[Fresh clones build from any drive](docs/shooter-enhancements.md#37-portable-windows-build-instructions)** — no machine-specific absolute paths are required.
38. **[Release versions are reproducible](docs/shooter-enhancements.md#38-reproducible-release-versioning)** — tags, packages, rebuilds, and `--version` stay identical.
39. **[Newer official hashcat fixes are included](docs/shooter-enhancements.md#39-newer-upstream-correctness-fixes)** — upstream fixes after the fork are preserved and clearly credited.
40. **[Huge rule files load across CPU cores](docs/shooter-enhancements.md#40-parallel-rule-file-loading)** — all rule-capable algorithms spend less time waiting at `Loading rules`.
41. **[`Restore.Sub` status lines are optional](docs/shooter-enhancements.md#41-optional-restoresub-status-lines)** — they stay hidden unless `--status-restore-sub` is requested.
42. **[Prebuilt releases run on standard x64 CPUs](docs/shooter-enhancements.md#42-portable-prebuilt-windows-binaries)** — release binaries do not inherit the GitHub runner's CPU-only instructions.
43. **[`--show` and `--left` finish faster on huge lists](docs/shooter-enhancements.md#43-faster-show-and-left)** — large potfiles use narrower lookups, and `-o` jobs skip a redundant full-result copy and sort.
44. **[Linux builds complete successfully](docs/shooter-enhancements.md#44-working-linux-builds)** — Shooter's mdxfind bridge now links in both static and shared Linux builds.
45. **[Errors are saved in one support file](docs/shooter-enhancements.md#45-automatic-error-reports)** — the file records recent warnings, every normal error, later warnings, and the details needed to investigate the problem.
46. **[Parser bugs are hunted automatically](docs/shooter-enhancements.md#46-sanitizers-and-parser-fuzzing)** — scheduled sanitizer and coverage-guided fuzz tests retain crash inputs.
47. **[`shooterctl` handles repeatable operations](docs/shooter-enhancements.md#47-shooterctl-companion)** — one included companion covers diagnostics, plans, streams, indexes, and GPU fleets.
48. **[`doctor` creates a privacy-limited support bundle](docs/shooter-enhancements.md#48-doctor-and-support-bundles)** — check an installation without collecting hashes, candidates, potfiles, or command lines.
49. **[Pipeline time and peak RAM can be measured](docs/shooter-enhancements.md#49-stage-time-and-peak-memory)** — opt-in human and JSON reports show where a run spent its time.
50. **[Huge rule sets can be inspected in ranges and run sequentially](docs/shooter-enhancements.md#50-rule-list-intelligence)** — report selected ranges, then run separate rule files one after another.
51. **[Huge line files have persistent indexes](docs/shooter-enhancements.md#51-persistent-line-indexes)** — `.hcidx` sidecars make seekable stream resumes jump near the saved line.
52. **[Partial, zstd, resumable, and piped attacks work](docs/shooter-enhancements.md#52-streaming-pipeline)** — stream slices directly or connect a hybrid/combinator producer to a cracking consumer.
53. **[Commands can become reviewable target manifests](docs/shooter-enhancements.md#53-target-manifests-and-import)** — import, inspect, plan, and run `shooter-target-v1` JSON jobs.
54. **[Twelve GPUs can share an adaptive work queue](docs/shooter-enhancements.md#54-adaptive-gpu-fleet)** — finished devices take more work while failed devices retry and quarantine.
55. **[Hash modes can be searched and explained locally](docs/shooter-enhancements.md#55-mode-search-and-explanation)** — find standard and mdxfind modes from the installed binary.
56. **[Releases include an SBOM and signed attestations](docs/shooter-enhancements.md#56-sbom-and-signed-release-attestations)** — verify archive contents, provenance, and the software inventory.
57. **[Slow-producing attacks can move on automatically](docs/shooter-enhancements.md#57-automatic-low-crack-rate-bypass)** — set a time window and minimum number of new cracks, then skip the current dictionary or mask when its yield falls below that minimum.
58. **[Existing outfile results are removed before cracking starts](docs/shooter-enhancements.md#58-outfile-check-before-cracking-allocation)** — if every hash is already in `--outfile-check-dir`, the expensive attack-specific GPU and host-memory allocation is skipped.
59. **[Huge wordlists index and feed faster](docs/shooter-enhancements.md#59-faster-large-wordlist-indexing-and-feed)** — first-use line counting uses the CPU cores and ordinary candidates reach the GPUs with less per-word overhead.

## Download and run

Download the single `shooter_hashcat-<version>-windows-x64-complete.7z` asset from the
[latest release](https://github.com/Shooter3k/shooter_hashcat/releases/latest)
and extract it. The archive contains the complete tagged source and the
already-built Windows x64 program.

Verify the package and check the version:

```powershell
.\verify-windows-package.ps1
.\hashcat.exe --version
.\shooterctl.exe doctor
```

If a run reports an error, look for `Error report saved to:` in the console.
Review that text file and send it with a short description of the problem; see
[Automatic error reports](docs/error-reports.md) for privacy details and
limitations.

Rebuild everything from the included source:

```powershell
.\build-windows.ps1 -Action Rebuild
```

The first rebuild downloads a checksum-pinned MSYS2 compiler toolchain into
the local `.build-tools` directory. Allow internet access and at least 5 GB of
free disk space. Nothing is installed system-wide, and the system or user
`PATH` is not changed. GPU vendor drivers remain an external requirement.

Tagged Windows releases are rebuilt on a clean GitHub runner. The release is
published only after the executable version, archive layout, source manifest,
and package-integrity checks pass. Separate CI jobs exercise static and shared
Windows and Linux builds, Rust crates, and the sanitizer-backed parser fuzzer.

## Technical details

| Topic | Documentation |
| --- | --- |
| Complete Shooter feature inventory | [docs/shooter-enhancements.md](docs/shooter-enhancements.md) |
| Startup, memory, parsing, and sorting | [docs/startup-optimization.md](docs/startup-optimization.md) |
| Large-wordlist indexing and feed speed | [wordlist I/O optimization](docs/startup-optimization.md#large-wordlist-indexing-and-feed-throughput) |
| Existing `--outfile-check-dir` results before cracking | [outfile-check startup](docs/startup-optimization.md#existing-outfile-results-before-cracking-allocation) |
| RTX 4090 autotune cache | [RTX_4090_AUTOTUNE_CACHE.md](RTX_4090_AUTOTUNE_CACHE.md) |
| Multi-file combinations and complete-candidate rules | [docs/multi-file-combination.md](docs/multi-file-combination.md) and [docs/whole-candidate-rules.md](docs/whole-candidate-rules.md) |
| Runtime and checkpoint controls | [docs/runtime-controls.md](docs/runtime-controls.md) and [docs/checkpoint-control.md](docs/checkpoint-control.md) |
| Automatic low-crack-rate bypass | [docs/bypass-rate-control.md](docs/bypass-rate-control.md) |
| Resumable candidate output | [docs/stdout-sessions.md](docs/stdout-sessions.md) |
| mdxfind modes | [docs/mdxfind-modules.md](docs/mdxfind-modules.md) and the [complete JSON registry](docs/mdxfind-modules.json) |
| Windows builds | [how_to_compile.txt](how_to_compile.txt) |
| Linux builds | [BUILD.md](BUILD.md) |
| Error reports and privacy | [docs/error-reports.md](docs/error-reports.md) |
| Companion commands, manifests, streams, indexes, and fleets | [docs/shooterctl.md](docs/shooterctl.md) |
| Stage timing and peak memory | [docs/stage-profile.md](docs/stage-profile.md) |
| Sanitizers and parser fuzzing | [docs/security-testing.md](docs/security-testing.md) |
| SBOM and signed release attestations | [docs/release-security.md](docs/release-security.md) |
| Every release and verification result | [CHANGELOG.md](CHANGELOG.md) |
| Source comparison | [Feature origins](docs/shooter-enhancements.md) and the [complete comparison with the upstream baseline](https://github.com/Shooter3k/shooter_hashcat/compare/fdad9f2f7bd7ec7f53056727e39331a17514db7c...master) |

## Upstream hashcat and license

General hashcat help is available from the [Hashcat Wiki](https://hashcat.net/wiki/),
[`--help`](docs/hashcat-help.md), the
[Hashcat Forum](https://hashcat.net/forum/), and
[Discord](https://discord.gg/HFS523HGBT).

hashcat and the Shooter modifications are licensed under the MIT license. See
[docs/license.txt](docs/license.txt).
