# hashcat_shooter release notes

## v7.1.2-shooter.20260813.23

Complete functional verification for the mdxfind named-module layer.

### Fixed

- Replaced the incomplete compatibility-expression path with Hashpipe's
  tested in-process verifier implementations, retaining mdxfind's expression
  VM as a fallback. The bridge remains self-contained and launches no helper
  process.
- Routed 38 `eN` wrappers away from similarly named numeric hashcat modes
  whose parser or exact computation differs from mdxfind. Existing numeric
  hashcat modules remain unchanged.
- Added mdxfind `$HEX[...]` binary-salt decoding, the two capitalization steps
  required by `e355`, and compatibility with the documented 16-byte `e539`
  MYSQL5MD5 prefix.
- Expanded bridge results to preserve complete long-form targets such as
  `e943` WPA-EAPOL records. A new comparison-kernel ID prevents stale CUDA
  cache entries from using the previous temporary-buffer layout.
- Updated `e942` to the current hashcat mode 22000 WPA parser instead of the
  deprecated mode 16800 path.
- Made the vendored mhash integer-size configuration portable across Windows
  LLP64 and Unix LP64 builds and included the upstream license notices.

### Coverage

- The generated inventory now uses 264 isolated wrappers around exactly
  compatible numeric hashcat modes and 737 mdxfind compatibility-bridge
  modules.
- `e426` (`PARALLEL`) is an mdxfind scheduler pseudo-entry rather than a hash
  algorithm. `e535` (`SHA1-CUSTOMUSERSALT`) depends on mdxfind's external
  custom-user/salt state and has no published standalone test vector. Both
  names remain present without being redirected to an incorrect algorithm.

### Verified

- Forced a complete Windows production rebuild, including all 1,001 `eN`
  module DLLs, the bridge, its static dependency archives, and the new CUDA
  comparison kernel. The executable reports
  `v7.1.2-shooter.20260813.23`.
- Ran every one of the 988 test hashes in Hashpipe's `HASH_TYPES.md` as an
  independent CUDA straight-attack known-answer test. All 988 recovered the
  documented password with no initialization failures, parse errors,
  exhaustions, or timeouts.
- Re-ran the six edge vectors that exposed defects (`e283`, `e348`, `e349`,
  `e355`, `e539`, and `e943`) after rebuilding; all six passed on CUDA.
- Added direct CUDA known-answer runs for the four standalone pre-995 entries
  omitted from `HASH_TYPES.md` (`e429`, `e432`, `e433`, and `e676`) and for
  every post-document mode `e995` through `e1001`; all eleven passed. Together
  with the 988 documented vectors, this exercises all 999 standalone hash
  modes in the 1,001-entry registry.

## v7.1.2-shooter.20260813.22

Magento input compatibility for mdxfind `e987` Argon2.

### Fixed

- `e987` now accepts both standard `$argon2...` PHC strings and mdxfind's
  Magento `hex_digest:salt:2` /
  `hex_digest:salt:3_digest_length_iterations_memory_bytes` forms.
- Magento inputs are converted to hashcat mode 34000 parameters internally
  using mdxfind's version-19, single-lane, first-16-salt-character rules.
- The original Magento hash line is retained for potfiles, `--show`, `--left`,
  and cracked-output files rather than being replaced with the internal PHC
  representation.
- The module generator now records specialized compatibility aliases so
  regenerating all 1,001 wrappers preserves the `e987` parser.

### Verified

- Parsed all 31,161 hashes in the supplied Magento `e987` file with zero
  errors and confirmed `--left` returned every original line unchanged.
- Completed an `e987` benchmark with the CUDA backend across all 12 detected
  RTX 4090 devices. `-O` correctly reports that no optimized Argon2 kernel is
  required and uses `m34000-pure.cl`.
- Confirmed standard Argon2 PHC input and the existing numeric mode 34000
  remain unchanged.

## v7.1.2-shooter.20260813.21

Consistent public `eN` names throughout the hashcat interface.

### Fixed

- `-hh` now lists all 1,001 mdxfind modes as `e1` through `e1001` instead of
  exposing their private integer representations.
- `-H`, `-HH`, example-hash output, runtime status, benchmark headings and
  machine-readable rows, autodetect results, diagnostics, source-module hints,
  and session logs now use the same public `eN` spelling.
- The main help table now describes `-m` as a mode and includes `-m e987` as
  an example. Existing numeric modes keep their original output unchanged.

### Verified

- Confirmed `-hh` contains exactly 1,001 named rows and maps representative
  entries to `e1`, `e987`, and `e1001` without exposing `90001`
  through `91001` as public mode values.
- Confirmed human-readable and JSON hash information report `e987`, while
  representative numeric modes retain their original numeric identifiers.

## v7.1.2-shooter.20260813.20

Named hashcat modules for the complete mdxfind algorithm registry.

### Added

- Added `module_e1` through `module_e1001`, matching every live algorithm name
  in mdxfind's `Types[]` registry. Hashcat now accepts names such as `-m e987`
  and loads the corresponding `module_e987` plugin.
- Kept every existing numeric hashcat module unchanged. The generated layer
  uses 302 isolated aliases to compatible hashcat modules and 699 isolated
  front ends for mdxfind's checked-in expression VM.
- Added the native `bridge_mdxfind` CPU evaluator and a small OpenCL comparison
  kernel. The bridge is self-contained in the Windows package and does not
  require a separate OpenSSL runtime DLL.
- Added `tools/generate_mdxfind_modules.py`, which reads mdxfind's live
  `Types[]` and `Maphashcat[]` tables rather than the potentially older prose
  list, plus `docs/mdxfind-modules.json` as the complete generated inventory.

### Compatibility and diagnostics

- Existing numeric `-m` values and numeric module filenames retain their
  original parsing and loading behavior.
- Mapped algorithms retain the original hashcat parser, self-test, kernel, and
  hash syntax while reporting their mdxfind `eN` name.
- Expression algorithms accept `hash[:salt[:salt2[:pepper[:user]]]]`. If the
  checked-in mdxfind expression table marks an algorithm as an outlier or
  needs a primitive unavailable in this bridge build, startup reports that
  exact limitation instead of selecting a different algorithm.

### Verified

- Production-built representative native aliases `e536` and `e987`, the
  translated expression module `e996`, the mdxfind bridge, and the main
  executable with the MinGW64 toolchain.
- Confirmed `hashcat.exe -m e536 --example-hashes`, `-m e987`, and `-m e996`
  resolve their named module files and report internal names `mdxfind eN ...`.
- Loaded all 1,001 generated modules with `--example-hashes`; every `e1`
  through `e1001` module resolved successfully.
- Confirmed the e996 expression VM produces the independently calculated
  `md5(sha256(sha256("hashcat")))` known answer.

## v7.1.2-shooter.20260812.19

Faster preprocessing for very large unsalted hash lists.

### Added

- Lists with at least 4,194,304 unsalted hashes now use a stable, parallel
  least-significant-digit radix sort with up to 64 CPU workers. The byte-pass
  order exactly matches hashcat's existing digest comparator.
- Smaller lists and all salted lists retain the upstream comparison sorter.
  If scratch allocation is unavailable, the optimized path safely falls back
  to that sorter. Set `HASHCAT_HASH_SORT_RADIX_DISABLE=1` to force the legacy
  path for comparison or troubleshooting.
- If Windows cannot create one of the requested worker threads, that worker's
  range is processed synchronously so no hashes can be omitted.

### Measured and verified

- On the intended 64-core/128-thread Threadripper PRO 5995WX system, the
  supplied 1.33 GB mode-0 list contains about 40.3 million hashes. An identical
  `--left` preprocessing pass took 48.85 seconds with the parallel sorter and
  80.50 seconds with it disabled: 31.65 seconds saved, 39.3% less total time,
  or 1.65x faster overall.
- Parallel and legacy output was byte-for-byte identical on 300,000- and
  2,000,000-hash validation sets. The optimized full 40.3-million-hash pass
  also completed parsing, sorting, duplicate removal, and output successfully.

## v7.1.2-shooter.20260812.18

Official hashcat synchronization through August 12, 2026.

### Integrated from official hashcat

- Merged all twelve official commits after the Shooter baseline
  `fdad9f2f7` through official master `9c735bade`, preserving their commit
  ancestry in the local merge.
- Added official attack mode `12`, the general multi-hybrid mode introduced by
  `554c1207a`. Its mask uses `?w` for the first wordlist and optional `?q` for
  a second wordlist, allowing masks and literals on either side of the words.
- Included official feed-to-device mapping, yescrypt address-space and layout
  fixes, PDF mode 10500 empty-ID support, hash/salt pointer-overflow fixes,
  full-length combinator plaintext/status buffers, and rule-processor
  overflow, double-free, and out-of-bounds fixes.

### Shooter compatibility

- Preserved Shooter's three-or-more-file `-a 1` Cartesian combinator and its
  multi-GPU range seeking, while no-rule two-file `-a 1` now uses the official
  mode-12 alias path.
- Preserved whole-candidate `-r`/`-g` processing for modes `1`, `3`, `6`, and
  `7`. Rule-bearing commands stay on the Shooter paths instead of being
  rewritten to official mode 12.
- Preserved all 12 x RTX 4090 startup, staging, autotune-cache, checkpoint,
  outfile-retry, resumable-stdout, runtime-control, and custom hash-mode work.
- Restored the custom combinator's no-mask update path and host-to-device base
  upload around the upstream multi-hybrid refactor. This prevents a
  three-file `-a 1` access violation and incomplete `--stdout` candidates.
- Restored recovered-plaintext reconstruction for Shooter's multi-file
  combinator, so cracked `-a 1` candidates are written to the outfile instead
  of appearing with an empty plaintext after the upstream refactor.
- Restored the legacy hybrid argument slicing and mask initialization needed
  by Shooter's whole-candidate-rule `-a 6` and `-a 7` paths.
- Kept compatibility mode `67000` on the maintained yescrypt kernel used by
  mode `36100`, matching its documented alias behavior after the upstream
  yescrypt host/kernel layout change.
- Updated the README and `hashcat.exe -h` attack-mode table to identify rule
  support for modes `0`, `1`, `3`, `6`, `7`, `8`, and `9`, with the native
  versus whole-candidate rule behavior documented explicitly.

### Verified

- Built the merged sources with debug symbols while resolving integration
  issues and confirmed the three-file access violation is gone.
- Verified exact candidate sets for official one- and two-wordlist `-a 12`,
  the official no-rule two-file `-a 1` alias, Shooter three-file `-a 1`, and
  Shooter whole-candidate rules on modes `1`, `3`, `6`, and `7`.
- Verified three-file `-a 1` reports keyspace `4`, total candidates `8`, and
  outputs all eight expected candidates in command-line order.
- Verified GPU known-answer cracks for official mode `12`, Shooter multi-file
  and ruled mode `1`, custom modes `29950` and `29951`, yescrypt modes `36100`
  and `67000`, and gost-yescrypt mode `36200`.
- Verified mode `10500` accepts the newly supported empty PDF ID form.

## v7.1.2-shooter.20260812.17

Responsive RTX 4090 tuning for phpBB3 bcrypt-over-phpass modes.

### Fixed

- Modes `29950` and `29951` now cap automatic tuning at `Accel:8` and
  `Loops:8`. The generic staged autotuner measures their inexpensive first
  phpass-MD5 loop, so it previously selected as much as `Accel:488` and
  `Loops:1024` for the much slower bcrypt loop2 stage.
- The oversized selection created roughly 1.5 million bcrypt candidates in a
  single GPU batch on an RTX 4090. Status, `--runtime`, interactive quit, and
  checkpoints could then wait minutes for a launch boundary even though CUDA
  compute was active.
- The tuning-range change is part of the persisted-autotune cache key, so old
  mode-29950/29951 entries are ignored automatically; users do not need to
  delete cache files.

### Measured

- On one RTX 4090 against a real cost-10 record with phpass count character
  `9`, completed-candidate throughput measured 6,485 H/s at `Accel:1`,
  6,799 H/s at `Accel:2`, 6,967 H/s at `Accel:4`, 7,090 H/s at `Accel:8`, and
  7,078 H/s at `Accel:16`. `Accel:8` retained the best measured throughput.
- A twelve-GPU reproduction of the reported command showed 100% CUDA/SM
  utilization on all twelve RTX 4090s. The memory-controller metric remained
  near zero, which is expected for this compute/shared-memory-heavy Blowfish
  workload and is not an idle-GPU measurement.
- The supplied hash file contains 1,853 valid 73-character records and five
  malformed records (four length 70 and one length 71); malformed records
  remain rejected rather than being guessed or silently repaired.

## v7.1.2-shooter.20260812.16 (local source build)

Native GPU support for phpBB3 bcrypt-over-phpass legacy rehashes.

### Added

- Added mode `29950` for `bcrypt(phpass($pass))` hashes stored by phpBB3 in
  their original `$H\2*$...` legacy-rehash form.
- Added mode `29951` for the explicitly selected rare
  `bcrypt(phpass(md5($pass)))` pipeline. The two pipelines are separate modes
  so a candidate is never silently transformed with the wrong construction.
- Both modes accept the original 73-character phpBB form directly and the
  extracted `$2*$...:<count><phpass-salt>` representation. Recovered hashes
  are rendered in the original phpBB form.
- The parser carries the per-hash phpass count character, eight-character
  phpass salt, bcrypt variant, bcrypt cost, 22-character bcrypt salt, and
  31-character bcrypt digest into the normal hashcat salt/digest paths.

### Implementation

- The GPU pipeline uses hashcat's staged slow-hash scheduler: phpass MD5 runs
  in `init/loop`, its 16-byte result is encoded to the 22-character portable
  phpass checksum, and that checksum becomes the bcrypt password for
  `init2/loop2/comp`.
- Only the 22-character phpass checksum is passed to bcrypt; the `$H$`
  signature, count character, and phpass salt are not part of the outer
  bcrypt password.
- The normal and MD5-prehash modes use distinct kernel types and self-tests.
  Both retain standard attack modes, rules, restore, status, outfile, and
  multi-GPU scheduling behavior. `-O` safely falls back to the pure kernel.

### Verified

- Independently recomputed the supplied example: phpass count character `7`
  selects `2^9 = 512` iterations, salt `RsqOrLNk`, and plaintext `123456`
  produce checksum `cmD0qyV8sv/3sGcLRo8D31`.
- Cracked the supplied original hash as `123456` with mode 29950 on CUDA and
  repeated the crack from its extracted representation.
- Independently generated and cracked a mode-29951 test vector for
  `123456`; its inner MD5 text is
  `e10adc3949ba59abbe56e057f20f883e`, its phpass checksum is
  `B5k0g0vxFC8hKXhGxEITa0`, and both CUDA self-tests pass.
- Verified a nondefault phpass count (`8`, or 1024 iterations), bcrypt `$2b$`
  cost 04, mixed hashes with different salts/costs, same-salt hashes with
  different bcrypt costs, a 100-character plaintext, an ordinary rule attack,
  the `-O` fallback, and initialization across all twelve RTX 4090s.
- Confirmed invalid phpass counts and bcrypt costs are rejected before GPU
  work starts.

## v7.1.2-shooter.20260812.15

Mode-1 whole-candidate-rule status correction.

### Fixed

- A two-wordlist `-a 1` attack using `-r` or `-g` now reports the actual
  left-side wordlist in `Guess.Base` instead of `File ((null))`.
- The error was limited to status presentation: the combinator candidate
  pipeline already retained and processed both wordlists. Combinator status
  now resolves its dictionary names before consulting the generic feed label.

### Verification

- Rebuilt the Windows production executable and confirmed
  `hashcat.exe --version` reports `v7.1.2-shooter.20260812.15`.
- Reproduced the reported two-wordlist `-a 1 -r` layout with the same input
  wordlists and rule file. Interactive status reported the correct left and
  right paths with no `(null)` value.
- Verified deterministic `--stdout` candidates for the two-file final-rule,
  three-file final-rule, and unchanged two-file no-rule paths as
  `ShooterStatus!`, `ShooterStatusCheck!`, and `ShooterStatus` respectively.

## v7.1.2-shooter.20260812.14 (local source build)

Dynamic multi-file combination through attack mode 1.

### Added

- Attack mode 1 now accepts two or more wordlists and concatenates one word
  from every file in command-line order. There is no fixed six-file array in
  the implementation.
- Three-or-more-file attacks use the pipelined host producer for the first
  `N - 1` files and the existing GPU combinator amplifier for the final file.
- Multi-GPU range startup converts the assigned Cartesian offset directly to
  its mixed-radix wordlist positions. A later GPU no longer replays all base
  combinations assigned before its range.
- Whole-candidate `-r` and `-g` rules work with any supported number of mode-1
  files. `-j` applies to file 1, `-k` applies to files 2 through `N`, and the
  ordinary rule then applies after concatenation.

### Changed

- Removed private attack modes 11, 12, 13, and 14. Their fixed three- through
  six-file layouts are now written as `-a 1` with the same wordlists, and mode
  1 also permits more files.
- The original two-file, no-whole-rule mode-1 path remains unchanged and keeps
  the native optimized combinator behavior.
- Status reports every multi-file input as `Guess.File.#NN`; help and usage no
  longer list modes 11-14.
- Updated the README, Windows compilation notes, change summary, combination
  guide, and whole-candidate-rule guide for the new syntax and accounting.

### Compatibility

- Three-or-more-file mode 1 is rejected with `-S/--slow-candidates` and brain
  client operation. The unchanged two-file no-rule path retains its prior
  compatibility.
- The implementation detects 64-bit Cartesian-product overflow. Candidate
  length and Windows command-line length remain practical upper bounds on the
  number of inputs.
- Without whole-candidate rules, `--keyspace` and restore positions count the
  product of files 1 through `N - 1`; `--total-candidates` includes file `N`
  as the GPU amplifier. With whole-candidate rules, all files form the base and
  the loaded rules are the amplifier.

### Verified

- Forced a full Windows MSYS2/MinGW64 production rebuild after the structure
  changes and confirmed `hashcat.exe --version` reports
  `v7.1.2-shooter.20260812.14`.
- Confirmed modes 11-14 are absent from help and each is rejected as an invalid
  attack mode.
- Verified exact `--stdout` Cartesian ordering with two, three, and eight
  wordlists; repeated the three- and eight-file tests with a `$!`
  whole-candidate rule.
- Ran RTX 4090 MD5 known-answer cracks through the unchanged two-file path, the
  eight-file path, and the eight-file whole-rule path. Every expected plaintext
  was recovered. Also verified `-j`, `-k`, and final `-r` ordering as
  `a1b2c2d2!` across four files.
- Verified `--skip 3 --limit 2` resumes at the correct three-file base work
  unit and emits only its two final-word amplifications.
- Requested an interactive checkpoint at 47.19% of a three-file attack,
  restored the saved session, and verified it resumed from that base position
  and exhausted at exactly 100% without repeating the prefix.
- Ran a 1,000,000,000,000-candidate three-file MD5 attack on all twelve RTX
  4090s. All devices initialized and reported nonzero sustained speed; status
  listed all three files and the attack exhausted the exact total. The final
  short-run aggregate was 645.4 GH/s on that test workload.

## v7.1.2-shooter.20260812.13 (local source build)

Whole-candidate rules on existing attacks and visible quit progress.

### Added

- Added optional `-r`/`--rules-file` and `-g`/`--generate-rules` processing to
  attack modes 1, 3, 6, 7, and 11-14. The rule is applied after the mode has
  assembled its complete candidate, not to only one component.
- Attack mode 8 retains its upstream native rule support. Mode 9 also retains
  its existing native association-rule behavior.
- Whole-candidate rule runs preserve stacked rule files, generated rules,
  `--stdout`, keyspace/total-candidate accounting, skip/limit, status,
  checkpoint, and restore paths supported by the underlying attack.
- Standard combinator side rules keep their normal order: `-j` and `-k`
  transform their respective mode-1 inputs before concatenation, and `-r` or
  `-g` then transforms the completed candidate.
- Pressing `q` or `Q` now prints progress while shutdown proceeds: candidate
  dispatch/GPU-kernel drain, GPU-worker completion, session-service shutdown,
  GPU-resource release, and final restore/session-file finalization.

### Changed

- Removed the unreleased attack-mode-15 implementation. Its use case is now
  handled directly by `-a 1 ... -r rules`, so existing attack numbering and
  the optimized no-rule combinator path remain intact.
- Modes 1, 3, 6, 7, and 11-14 retain their original fast GPU paths whenever no
  ordinary rule file or generated-rule request is supplied. The new host-side
  complete-candidate producer is selected only for a whole-candidate rule run.
- Status identifies the native attack layout and separately reports the rule
  source as `Whole Candidate`, rather than presenting a synthetic attack mode.

### Compatibility

- `--slow-candidates` and brain-client operation are rejected for the new
  whole-candidate rule paths. The existing behavior without whole-candidate
  rules is unchanged.
- Candidate-length checks occur after the mode's inputs are assembled and
  before the GPU rule is applied, matching straight-kernel base-word behavior.

### Verified

- Clean-built and versioned the Windows production binary as
  `v7.1.2-shooter.20260812.13`.
- Ran RTX 4090 known-answer MD5/rules tests for modes 1, 3, 6, 7, 8, and 11-14.
  The recovered candidates confirmed that `$!` was applied to the completed
  candidate in every mode.
- Repeated known-answer tests without `-r` for the same nine modes, confirming
  the existing native paths and candidate layouts still work.
- Verified `--stdout` output for ruled modes 1, 3, 6, 7, 8, and 11-14 byte for
  byte; the emitted values ranged from `abcd!` for mode 1 through `abcdef!`
  for mode 14. Confirmed generated-rule accounting with `-g 3` as well.
- Ran a generated-rule mode-3 workload on all twelve RTX 4090 devices. Every
  GPU initialized, received work, and reported a nonzero speed until the
  intentional five-second runtime limit.
- Confirmed a live long-running GPU session accepts quit and prints each new
  shutdown-progress stage before its final Started/Stopped/Total Time summary.

## v7.1.2-shooter.20260812.10 (local source build)

Atomic CUDA startup retry for multi-GPU jobs.

### Changed

- Extended the existing CUDA startup recovery to `cuStreamCreate()` and CUDA
  event-creation failures. Stream and event creation retry in place on the
  affected context, avoiding the cost and state churn of rebuilding the attack.
- A failure on any selected CUDA device now rejects the complete startup
  attempt. Hashcat no longer continues a twelve-GPU command using only the
  subset whose contexts happened to initialize.
- Partial resources from a failed context-startup attempt are released before
  retrying, preventing the next clean attempt from inheriting memory pressure.
- Increased the delay between attempts from two to five seconds. Context,
  stream, and event creation each retain ten retries after the initial attempt.
  Exhaustion prints an explicit message and exits instead of falling through to
  a partial-GPU run.

### Verified

- Completed a forced full Windows MSYS2/MinGW64 production rebuild and
  confirmed `hashcat.exe --version` reports
  `v7.1.2-shooter.20260812.10`.
- Completed a normal mode-1800 benchmark on all 12 RTX 4090 devices.
- In a temporary test-only build, injected one context-creation-stage failure,
  one stream-creation failure, and one event-creation failure. Each retry path
  recovered, and the mode-1800 benchmark then completed on all 12 devices with
  exit code 0. The fault-injection hooks were removed before the final build.

## v7.1.2-shooter.20260812.9 (local source build)

Interactive outfile-check keep-going control.

### Added

- Added `[k]eep-going` to the interactive menu whenever outfile-directory
  checking is active.
- Pressing `k` stops further `--outfile-check-dir` processing for the current
  run without deleting, truncating, or modifying anything in that directory.
- The checker and key handler synchronize at outfile-line boundaries. A line
  already being processed may finish, but after `k` takes effect no later line
  can mark another hash as cracked.

### Compatibility

- Hashes processed before `k` remain marked as cracked. The control applies to
  the current process only; a new run or `--restore` starts with the configured
  outfile checker enabled again.
- The key is hidden when outfile checking is disabled, unavailable for the
  selected mode, or already bypassed.

### Verified

- Completed a forced full Windows MSYS2/MinGW64 production rebuild and
  confirmed `hashcat.exe --version` reports
  `v7.1.2-shooter.20260812.9`.
- On the target 12 x RTX 4090 system, pressed `k`, added a matching MD5 result
  to the watched directory, waited through multiple one-second check periods,
  and confirmed the attack remained `Running` with `Recovered: 0/1`.
- Repeated the same test without pressing `k` and confirmed the unchanged
  checker found the result after one second and completed as `Cracked` with
  `Recovered: 1/1`.

## v7.1.2-shooter.20260812.8

Resumable `--stdout` candidate-generation sessions.

### Added

- Enabled normal `.restore` checkpoints for `--stdout` sessions instead of
  forcibly disabling restore in stdout preprocessing.
- Added the normal interactive `[p]ause`, `[r]esume`, `[c]heckpoint`, and
  `[q]uit` menu to mask- and file-driven stdout sessions. Menu and event text
  are written to stderr so stdout remains a candidate-only stream.
- Added an exact outfile journal for `--stdout -o`: restore data now records
  both the committed candidate position and the matching byte boundary.
  `--restore` truncates any uncommitted tail before reopening the file in
  append mode.
- Ordered multi-GPU stdout batch commits by keyspace position. A restore file
  therefore always describes a contiguous output prefix even when later GPU
  batches finish first.
- Added pause/quit checks at buffered output boundaries, making host-side
  candidate generation responsive without adding a branch to every candidate.

### Compatibility

- Restore format version 721 reads existing version-720 cracking restore
  files. Exact stdout outfile restoration requires a version-721 restore file.
- Direct stdout and pipe sessions can resume their candidate position, but
  downstream bytes cannot be rolled back. Exact no-tail continuation requires
  a regular `-o/--outfile` file.
- A session that reads its candidate source from stdin cannot also use stdin
  for the interactive menu. File wordlists and masks use the menu normally.

### Verified

- Completed a clean Windows MSYS2/MinGW64 production build and confirmed
  `hashcat.exe --version` reports `v7.1.2-shooter.20260812.8`.
- Confirmed a 100-candidate mask run through `--stdout -o` wrote exactly 100
  candidates, wrote no candidate data to process stdout, and removed its
  restore file after successful exhaustion.
- Confirmed direct stdout still contains candidates only while warnings and
  the interactive menu are isolated on stderr.
- On the target Windows GPU system, confirmed `[p]ause` stopped outfile growth,
  a restore file was saved while paused, and `[r]esume` restarted generation.
- Appended an 8-byte uncommitted tail to a checkpointed 11,000,000,000-byte
  candidate file and confirmed `--restore` removed exactly those 8 bytes,
  logged the saved boundary, wrote nothing to process stdout, and completed
  without regenerating the exhausted keyspace.
- Enabled `[c]heckpoint` during a live stdout session and confirmed it exited
  at the coordinated multi-GPU boundary with an updated `.restore` file.

## v7.1.2-shooter.20260812.7

Transient Windows outfile-access recovery.

### Added

- Added bounded retry handling when the `-o` path temporarily returns
  `Permission denied`, `busy`, or another retryable access error. Startup path
  validation and result-time append opens retry for up to 5 seconds at 250 ms
  intervals.
- Added a 30-second retry cooldown after a full retry window is exhausted.
  Later recovered results still make one immediate open attempt, but persistent
  failures cannot impose another 5-second stall or duplicate the same error for
  every result. A timed retry window becomes eligible again after the cooldown.
- Added explicit recovery messages when the outfile becomes available and
  result writing resumes.

### Performance

- Successful outfile opens retain the original single-open fast path. They do
  not sleep, read a timer, or enter retry bookkeeping, so normal cracking and
  outfile performance are unchanged. Delays occur only after an actual outfile
  access failure.

### Verified

- Completed a clean Windows MSYS2/MinGW64 production build and confirmed
  `hashcat.exe --version` reports `v7.1.2-shooter.20260812.7`.
- A startup `Permission denied` caused by a read-only outfile recovered after
  9 retries when the file became writable; the attack cracked and the expected
  result was written.
- A result-time `Permission denied` caused by an exclusive Windows file lock
  recovered after 11 retries when the handle was released; the attack cracked
  and the expected result was written.
- A persistent exclusive lock during a three-hash test produced exactly one
  5-second retry window and one final error. Later results used immediate
  attempts instead of adding two more 5-second delays.
- An unlocked warm-cache control run cracked normally, wrote the expected
  result, and emitted zero outfile retry messages.

## v7.1.2-shooter.20260812.6

RTX 4090 persisted-autotune cache validation correctness.

### Fixed

- Initialized the same synthetic candidates and rule buffer before cached
  profile validation that full autotune uses. Rule attacks no longer compare
  an uninitialized approximately 15 ms validation launch with the properly
  initialized approximately 31 ms stored launch and retune every run.
- Made cache validation tolerate expected timing variance while 12 GPUs tune
  concurrently. Faster launches are accepted, while slower launches are
  rejected only after exceeding both four times the stored duration and the
  selected workload target, subject to the existing 2-second safety ceiling.
- Serialized concurrent log formatting and display with a dedicated mutex, so
  simultaneous per-GPU cache messages retain their correct device numbers and
  no longer appear duplicated or interleaved.
- Advanced the local build revision to `v7.1.2-shooter.20260812.6`.

### Verified

- Completed a clean Windows production build and confirmed
  `hashcat.exe --version` reports `v7.1.2-shooter.20260812.6`.
- Ran the reported NTLM (`-m 1000`), straight/rules (`-a 0`), `-w 4`
  command from an empty cache on all 12 RTX 4090s: status `Cracked`,
  `479.8 GH/s`, 14 seconds, and exactly one profile saved.
- Repeated the identical command twice: both runs reused the one profile on
  all 12 devices, produced zero rejection and save messages, kept exactly one
  cache record, cracked the target, and measured `479.2` and `479.1 GH/s`.

## v7.1.2-shooter.20260812.5

Multi-GPU checkpoint cancellation reliability, elapsed-time reporting, and
12 x RTX 4090 short-session startup improvements.

### Added

- Added `Total Time` to the final status summary, calculated from the displayed
  `Started` and `Stopped` timestamps.
- Added `HASHCAT_SHOOTER_HOST_STAGING_MB` for changing the automatic per-GPU
  host candidate-staging limit. Set it to `0` to restore the generic limit.

### Changed

- Changed checkpoint requests into a coordinated device barrier. GPUs that
  reach a restore boundary first now remain parked with their worker threads
  alive instead of exiting while slower GPUs finish their current work.
- Cancelling a pending checkpoint now releases every parked GPU, so all
  devices that participated in the attack resume—not only the GPUs that were
  still executing when cancellation was requested.
- Paused candidate producers together with their GPU consumers so prefetched
  work remains intact across checkpoint cancellation.
- Counted only live, non-skipped GPU workers in the barrier and accounted for
  devices that naturally finish near the end of the keyspace.
- Carried forward the missing parallel CUDA context initialization and
  per-device teardown work from `M:\hashcat_shooter`.
- Initialized host candidate-staging buffers concurrently and avoided
  zero-filling data that candidate construction overwrites before use.
- Replaced full candidate-buffer resets with the required metadata reset.
- Limited the two-slot host candidate staging to 3072 MiB per GPU on the exact
  Windows 12 x RTX 4090 configuration. This reduced the tested mode-0 host
  allocation from approximately 97.7 GB to 36.7 GB.

### Verification

- Clean Windows MSYS2/MinGW64 production build passed.
- Rapid checkpoint enable/disable on a 12 x RTX 4090 bcrypt/rule attack kept
  the session running and restored fresh nonzero speeds on all 12 GPUs.
- Leaving the checkpoint enabled produced a clean `Aborted (Checkpoint)` and
  a saved restore point.
- Starting with `--restore` resumed from that checkpoint with all 12 GPUs and
  reused the persisted RTX 4090 autotune settings on all devices.
- The short mode-0 known-answer run improved from approximately 18-23 seconds
  to 15.8-16.9 seconds cold and 7.6-10.1 seconds warm on the 12-card system.
- Normal dictionary candidate processing and attack modes 11, 12, 13, and 14
  completed known-answer tests successfully after the staging changes.
- A sustained mode-0 comparison measured 686.3 GH/s at the lower-memory
  geometry and 693.1 GH/s at acceleration 96 (approximately 1% difference).

## v7.1.2-shooter.20260811.4

Windows startup optimization for the intended 12 x RTX 4090 system.

### Added

- Added an automatic CUDA-only fast-start path for normal cracking sessions
  when CUDA reports exactly twelve `NVIDIA GeForce RTX 4090` devices.
- Added `HASHCAT_SHOOTER_FAST_START=0` as a per-process override that restores
  full HIP and OpenCL probing.
- Added startup behavior, measurements, and override instructions in
  `docs/startup-optimization.md`.

### Changed

- Kept `-I`/`--backend-info` comprehensive; diagnostic runs still enumerate
  CUDA, NVIDIA OpenCL, and Intel OpenCL.
- Updated the Windows instructions to use `make PRODUCTION=1`, ensuring local
  production binaries embed the dated shooter version.
- Advanced the build revision to `v7.1.2-shooter.20260811.4`.

### Verification

- Clean Windows MSYS2/MinGW64 production build passed.
- Automatic detection found all twelve RTX 4090 devices and enabled fast-start.
- The override restored full backend probing.
- MD5 and legacy yescrypt mode 67000 known-answer cracks passed.
- Alternating fixed-tuning MD5 runs measured 6.12/6.17 seconds with fast-start
  versus 6.69/6.76 seconds with all backends, approximately 8.6% faster for
  the tested short session.

## v7.1.2-shooter.20260811.3

Dated build identifiers, inverse runtime control, and legacy yescrypt
compatibility.

### Added

- Added date-and-revision version identifiers in the form
  `v7.1.2-shooter.YYYYMMDD.REVISION`.
- Added interactive `[l]ower` runtime control. It advances the countdown at
  twice normal speed and can switch directly to or from `[e]xtend`.
- Restored legacy yescrypt hash mode 67000 as a compatibility alias for the
  current optimized mode 36100 implementation.
- Added documentation for runtime controls, mode 67000, and Windows builds.

### Verification

- Clean Windows build passed.
- Direct lower, extend-to-lower transition, runtime abort, and standard
  known-answer tests passed.
- RTX 4090 GPU known-answer cracks passed for modes 67000 and 36100.

## v7.1.2-shooter.20260811.1

First dated release of the shooter customization ported onto the newer
hashcat master codebase.

### Added

- Added attack modes 11 through 14 and documented multi-way combination use.
- Added interactive `[e]xtend` runtime control.
- Added persisted autotune profiles for identical RTX 4090 devices, including
  safe profile validation and invalidation.
- Added grouped autotuning so matching devices can share measured settings.
- Added CUDA initialization retry handling and Windows runtime/build guidance.
- Added custom modes 29960, 29970, 29980, and 29990 with their GPU kernels and
  operating notes.
- Added the ported performance, dispatch, status, candidate-processing, and
  multi-device improvements from the beta development tree.

### Verification

- Built and tested on the 12 x RTX 4090 Windows system.
- RTX 4090 autotune-cache cold/warm validation covered attack modes 0, 1, 3,
  6, 7, 8, 9, and 11 through 14, plus slow-candidate operation.
- Standard known-answer and custom-mode checks passed during the port.

## Version numbering note

Revision `.2` was an intermediate local build and was not published as a
GitHub release. Published releases therefore proceed from `.1` to `.3`.
