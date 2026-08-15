# Windows startup optimization for 12 RTX 4090 GPUs

Normal cracking sessions on the intended Windows system automatically use a
CUDA-only fast-start path when all of these checks pass:

- CUDA and NVRTC initialized successfully.
- CUDA reports exactly 12 devices.
- Every CUDA device reports the name `NVIDIA GeForce RTX 4090`.

HIP and OpenCL probing is redundant on this all-NVIDIA system. Avoiding those
runtimes reduces driver discovery and duplicate device setup.

The current build also carries forward the startup work from
`M:\hashcat_shooter`:

- CUDA contexts and their available-memory queries initialize concurrently.
- Per-device session buffers and CUDA contexts are released concurrently.
- Candidate staging buffers initialize concurrently and are not unnecessarily
  zero-filled before candidate construction overwrites them.
- Session reset initializes only the staging metadata that is read before the
  first candidate write, rather than touching every page of every buffer.

## Large hash-list sorting

Unsalted lists with at least 4,194,304 hashes use a stable parallel byte-radix
sort with up to 64 CPU workers. The pass order matches hashcat's digest-word
comparator exactly, so duplicate detection and every later lookup see the same
ordering as the original implementation. Smaller lists and salted hashes keep
the original comparison sorter because the parallel setup is not beneficial
there.

The optimized sorter allocates one temporary `hash_t` entry per input hash. On
the current 64-bit Windows build, the supplied 40.3-million-hash list uses
roughly 3 GiB of temporary scratch space during sorting. If that allocation is
unavailable, hashcat falls back to the original sorter. To force the original
sorter for an A/B test:

```powershell
$env:HASHCAT_HASH_SORT_RADIX_DISABLE = '1'
M:\github\hashcat_shooter\hashcat.exe ...
Remove-Item Env:HASHCAT_HASH_SORT_RADIX_DISABLE
```

On the 64-core/128-thread Threadripper PRO 5995WX system, an identical full
preprocessing pass over the supplied 1.33 GB mode-0 list took 48.85 seconds
with the optimized sorter and 80.50 seconds with it disabled. That is 31.65
seconds saved and 39.3% less total preprocessing time.

The newer two-slot candidate pipeline can otherwise reserve about 97.7 GB of
host staging memory on this system for fast hashes. On the exact 12-card RTX
4090 configuration, the default is therefore limited to 3072 MiB per GPU
across both slots. This retains producer/GPU overlap while avoiding tens of
gigabytes of startup page faults.

The CUDA-only probe and host-staging limit deliberately do not apply on
non-Windows builds or hardware configurations other than the exact 12-card RTX
4090 system. The CUDA-only probe also does not apply to `-I`/`--backend-info`,
so diagnostic output continues to enumerate every installed backend.

## Temporarily probe every backend

Set the override to zero for a run that needs HIP or OpenCL discovery:

```powershell
$env:HASHCAT_SHOOTER_FAST_START = '0'
M:\github\hashcat_shooter\hashcat.exe ...
```

Restore automatic behavior afterward:

```powershell
Remove-Item Env:HASHCAT_SHOOTER_FAST_START
```

The existing `--backend-ignore-*` options remain available and continue to
take precedence when explicitly supplied.

## Change the host-staging limit

Set a custom per-GPU limit in MiB before starting hashcat. The value covers
both candidate pipeline slots combined:

```powershell
$env:HASHCAT_SHOOTER_HOST_STAGING_MB = '4096'
M:\github\hashcat_shooter\hashcat.exe ...
```

Set the value to zero to disable the shooter-specific 3072 MiB limit and use
hashcat's generic limit:

```powershell
$env:HASHCAT_SHOOTER_HOST_STAGING_MB = '0'
M:\github\hashcat_shooter\hashcat.exe ...
```

Restore the optimized default afterward:

```powershell
Remove-Item Env:HASHCAT_SHOOTER_HOST_STAGING_MB
```

Increasing this value can allow a larger acceleration setting for fast hashes,
but also increases host memory commitment and the amount of memory Windows must
prepare at session startup.

## Measured result

For the short mode-0 known-answer case that prompted this update, the previous
GitHub build took about 18-23 seconds and reported roughly 97.7 GB of host
memory. With the carried-forward startup work and the 3072 MiB per-GPU staging
limit, the final test build reported about 36.7 GB and completed in 15.8-16.9
seconds from a cold state, followed by warm runs ranging from 7.6 to 10.1
seconds. The older
`M:\hashcat_shooter` comparison build completed the same test in about 10
seconds during this verification.

A sustained 12-GPU mode-0 benchmark measured 686.3 GH/s with the lower-memory
geometry versus 693.1 GH/s with acceleration 96, a difference of approximately
1 percent in that test. Users who prefer maximum fast-hash throughput over
short-session startup and host-memory use can raise or disable the staging
limit as described above.

## Large text hash-list parsing

Native-format text lists with at least 4,194,304 nonempty hashes use a
memory-mapped parser with up to 64 CPU workers. Mode 0 retains its specialized
hex validation and direct MD5 decoder. Other compatible modes pass each line
to the selected module's own decoder, preserving raw, salted, structured,
extended-salt, hook-salt, original-hash-copy, and compatible postprocess
formats. The file mapping is released before hash sorting begins.

Empty lines and mixed LF/CRLF endings retain the original behavior. If any
worker encounters malformed input, Hashcat clears the partial digest data and
reruns the complete file through the original sequential loader. This keeps
the original line-specific warnings and partial-file recovery semantics.

Required binary containers, split-hash formats, non-native hash-list formats,
compressed or BOM-prefixed inputs, username/dynamic parsing, association
autosplit, and postprocessors using external keyfiles or keyboard maps retain
the original loader. Optional-binary modes can participate when Hashcat has
classified the input as native text.

Set `HASHCAT_HASH_PARSE_PARALLEL_DISABLE=1` to force the original parser for an
A/B test:

```powershell
$env:HASHCAT_HASH_PARSE_PARALLEL_DISABLE = '1'
M:\github\hashcat_shooter\hashcat.exe ...
Remove-Item Env:HASHCAT_HASH_PARSE_PARALLEL_DISABLE
```

`HASHCAT_HASH_PARSE_MD5_DISABLE=1` remains a mode-0-compatible alias.
`HASHCAT_HASH_PARSE_PARALLEL_MIN` can set a lower activation count for focused
correctness tests or controlled benchmarks; production use should normally
leave the default unchanged.

On the Threadripper PRO 5995WX system:

- The supplied 2.87 GB mode-0 file contained 84,381,740 physical lines, one
  empty line, and 84,381,739 MD5 hashes. Parse plus sort fell from 33.56
  seconds to 6.41 seconds. Preprocessing through duplicate removal fell from
  48.12 seconds to 11.28 seconds.
- A 4,194,304-entry SHA-1 fixture completed `--left` in 0.57 seconds versus
  1.49 seconds with the original parser.
- A 4,194,304-entry salted mode-10 fixture completed `--left` in 1.36 seconds
  versus 2.48 seconds with the original parser.

BOM-free two-worker fixtures were also compared with the forced original
parser across all 854 modes exposing usable self-test examples. All 814 that
loaded successfully matched, and 39 special-file/option failures were
identical. Mode 32500 was excluded from output comparison because repeated
original-parser runs expose an existing nondeterministic trailing-byte encoder
defect.

The original v7.1.2-shooter.20260811.4 CUDA-only probe measurement is retained
below for reference.

Four alternating mode-0 known-answer runs were performed with fixed tuning to
remove autotune variability. The automatic fast-start runs completed in 6.12
and 6.17 seconds; full-backend runs completed in 6.69 and 6.76 seconds. That is
approximately 0.58 seconds, or 8.6 percent, removed from this short session.

Backend-information diagnostics took approximately 8.97 seconds with every
backend versus 4.74 seconds when manually limited to CUDA. Automatic fast-start
does not alter `-I`, but this comparison shows why probing unused runtimes can
be especially noticeable before the driver state is warm.

These are total process times and will vary with driver state, kernel-cache
state, hash mode, attack, and system load. The context/probe changes do not
alter hash kernels. The staging limit can influence autotuned acceleration, as
shown by the sustained benchmark above.
