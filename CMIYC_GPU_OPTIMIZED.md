# Optimized CMIYC GPU mode 29960

Mode 29960 is an optimized, separate version of CMIYC mode 29970. It keeps
mode 29970 available as a known-good fallback.

The optimized kernel bypasses Hashcat's generic streaming SHA-512 state
machine for CMIYC's fixed 64-byte, 82-byte, and 145-byte messages. It invokes
the SHA-512 compression primitive with prebuilt padding and processes 4,096
CMIYC steps per kernel launch.

## Measured results

On the 12 RTX 4090 system and the supplied 481-salt workload:

- Mode 29970, one Hashcat process: about 12 H/s, ETA 11h35m
- Mode 29960, one Hashcat process: about 17 H/s, ETA 8h20m
- Mode 29960, one salt shard on one GPU with hardware monitoring enabled:
  about 5 H/s per GPU

Twelve balanced one-GPU shards should therefore approach 50-60 H/s for this
specific 1,035-word workload, or roughly 2-3 hours. Exact speed varies with
temperatures, clocks, and how evenly the hashes divide.

## Why sharding helps

The wordlist contains 1,035 candidates, while Hashcat reports that the 12
devices need at least 2,796 base words to fill the configured acceleration.
A single process splits those 1,035 candidates across all GPUs, leaving each
GPU with too little independent work for every SM. Assigning a subset of salts
to each GPU lets every device run the full wordlist independently.

## Launch the balanced shards

```powershell
cd M:\junk\hashcat-beta

pwsh -File .\CMIYC_SHARDED_LAUNCH.ps1 `
  -HashFile 'M:\hmwm\20-cmiyc-2026-pro-cmiyc-custom-h1297\20.h1297.left' `
  -Wordlist 'M:\junk\bonjovi_greatest_hits_exact_lyrics_wordlist.txt' `
  -CombinedOutput 'M:\hmwm\20-cmiyc-2026-pro-cmiyc-custom-h1297\20.h1297.new'
```

The launcher starts one hidden Hashcat process per device. It creates separate
hash shards, sessions, potfiles, cracked outputs, and logs under a timestamped
directory beside the input hash file. When `-CombinedOutput` is supplied, the
launcher remains attached and safely merges each complete result into that file
while the GPU processes are running. Existing lines are preserved and duplicate
lines are not appended.

`-CombinedOutput` automatically enables `-Wait`. Without a combined output, add
`-Wait` to keep the launcher attached until every shard exits.
Use `-DryRun` to create and inspect the shards without starting Hashcat. A
subset can be selected with a comma-separated argument such as
`-Devices 1,2,3`.

Monitor one device log with:

```powershell
Get-Content -Wait '<output-directory>\logs\device_01.stdout.log'
```

## Important command-line details

- Do not use `--hwmon-disable`. On this system it reduced acceleration from
  350 to 233 and reduced the one-GPU shard from about 5 H/s to 3 H/s.
- `-O` does not select a different kernel for this pure outside-kernel mode.
- `--bitmap-max 26` has no meaningful effect because each candidate spends
  almost all its time inside the memory-hard KDF.
- Mode 29960 files are `src/modules/module_29960.c`,
  `OpenCL/m29960-pure.cl`, and `modules/module_29960.dll`.
