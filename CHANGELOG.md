# hashcat_shooter release notes

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
