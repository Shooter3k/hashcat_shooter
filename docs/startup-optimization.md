# Windows startup optimization for 12 RTX 4090 GPUs

Normal cracking sessions on the intended Windows system automatically use a
CUDA-only fast-start path when all of these checks pass:

- CUDA and NVRTC initialized successfully.
- CUDA reports exactly 12 devices.
- Every CUDA device reports the name `NVIDIA GeForce RTX 4090`.

HIP and OpenCL probing is redundant on this all-NVIDIA system. Avoiding those
runtimes reduces driver discovery and duplicate device setup without changing
hash kernels, attack modes, tuning, or cracking speed after startup.

The optimization deliberately does not apply to `-I`/`--backend-info`, so
diagnostic output continues to enumerate every installed backend. It also does
not apply on non-Windows builds or any hardware configuration other than the
exact 12-card RTX 4090 system.

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

## Measured result

Four alternating mode-0 known-answer runs were performed with fixed tuning to
remove autotune variability. The automatic fast-start runs completed in 6.12
and 6.17 seconds; full-backend runs completed in 6.69 and 6.76 seconds. That is
approximately 0.58 seconds, or 8.6 percent, removed from this short session.

Backend-information diagnostics took approximately 8.97 seconds with every
backend versus 4.74 seconds when manually limited to CUDA. Automatic fast-start
does not alter `-I`, but this comparison shows why probing unused runtimes can
be especially noticeable before the driver state is warm.

These are total process times and will vary with driver state, kernel-cache
state, hash mode, and system load. The optimization changes startup only; it
does not change steady-state cracking throughput.
