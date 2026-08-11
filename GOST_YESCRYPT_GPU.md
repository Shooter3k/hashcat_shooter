# GPU gost-yescrypt module (mode 29980)

This private Hashcat module implements libxcrypt-style gost-yescrypt hashes
with the `$gy$j9T$` setting on the GPU. The supported setting is the common
yescrypt default profile (`N=4096`, `r=32`, `p=1`, `t=0`, no ROM).

## Files

- `src/modules/module_29980.c` — host parser, encoder, memory sizing, and tuning
- `OpenCL/m29980-pure.cl` — CUDA/OpenCL kernel source
- `modules/module_29980.dll` — built Windows module
- `gy_test.dict` — supplied validation plaintext

## Usage

Run from the Hashcat directory:

```powershell
.\hashcat.exe -m 29980 .\hashes.txt .\wordlist.txt
```

To choose a particular device, add `-d <device-id>`. The kernel uses about
16 MiB of extra GPU memory per concurrent candidate, so Hashcat automatically
limits acceleration according to available device memory.

## Rebuild the Windows module

The module was built with the MinGW cross-compiler in WSL:

```powershell
wsl.exe -d Ubuntu-24.04 bash -lc "cd /mnt/m/junk/hashcat-beta && make modules/module_29980.dll -j2"
```

Hashcat caches compiled GPU kernels in `kernels/`. If the `.cl` source is
changed during development, remove only the cached `m29980-pure.*.kernel`
file before the next test so Hashcat recompiles it.

## Validation vector

```text
$gy$j9T$uUiI.N7T3Tz6d3pvC.pnB0$If1AACG./CCB6LPJ/dZBgxzkRCBEvheOmpOcvxfYK36:fixed-but forgot to
```

Validated with Hashcat 7.1.2, CUDA 12.6, and an NVIDIA GeForce RTX 4090.
The module passes Hashcat's device self-test and recovers the supplied
plaintext without `--force` or `--self-test-disable`.
