# GPU CMIYC 2026 module (mode 29970)

This private Hashcat module implements the CMIYC 2026 memory-hard SHA-512 KDF
described by `M:\hmwm\cmiyc_crack_v0.3.0\ALGORITHM.md`.

Mode 29970 is separate from and does not modify the pre-existing private mode
29990.

## Supported hashes

```text
$cmiyc$2026$<rounds>$<memlog>$<salt_b64url>$<digest_b64url>
```

- `rounds`: 1 through 32
- `memlog`: 10 through 24
- Salt: 16 bytes encoded as unpadded Base64URL
- Digest: 32 bytes encoded as unpadded Base64URL
- GPU memory per concurrent candidate: `64 * 2^memlog` bytes

## Files

- `src/modules/module_29970.c` — host parser, encoder, and memory tuning
- `OpenCL/m29970-pure.cl` — CUDA/OpenCL kernel source
- `modules/module_29970.dll` — built Windows module

## Usage

```powershell
.\hashcat.exe -m 29970 .\hashes.txt .\wordlist.txt
```

Use `-d <device-id>` to select a device. Hashcat limits acceleration according
to the largest loaded hash and the GPU memory currently available.

## Rebuild the Windows module

```powershell
wsl.exe -d Ubuntu-24.04 bash -lc "cd /mnt/m/junk/hashcat-beta && make modules/module_29970.dll -j2"
```

If the kernel source changes, remove only the generated
`kernels/m29970-pure.*.kernel` cache file before retesting.

## Validation

The module passes Hashcat's device self-test using the reference project's
low-cost vector. It was also validated on an NVIDIA GeForce RTX 4090 with the
full supplied sample:

```text
$cmiyc$2026$4$20$hmHrVCSVwCoI8haoygAktQ$2Ti8BnoIfIdDk5ESkwTSRa57UGXF6MVv7erUpSXyVZQ:Always
```
