# hashcat_shooter release notes

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
