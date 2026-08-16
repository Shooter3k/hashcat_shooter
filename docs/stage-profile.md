# Pipeline stage profile

Add `--stage-profile` to a normal Hashcat command to print a final timing and
peak-memory summary:

```powershell
hashcat.exe -m 0 hashes.txt words.txt --stage-profile
```

The report separates candidate feeding, host-to-device copy/decompression,
initialization, temporary-buffer transfers, main launches, and comparison.
It also reports the number of measured launches and peak process RAM. Feed
time runs on a producer thread and is intentionally not included in the
measured device-pipeline total, so overlapping work is not counted twice.

Use `--stage-profile-json` when a script or support tool needs one JSON object:

```powershell
hashcat.exe -m 0 hashes.txt words.txt --stage-profile-json
```

The `shooter-stage-profile-v1` object contains `launches`, `candidates`,
`measured_pipeline_ms`, `peak_memory_bytes`, and one millisecond value per
stage. Profiling is disabled by default, so ordinary runs pay no timer cost.

Multi-device measurements combine time spent by all selected device threads.
They answer which stages dominate the whole job; they are not a per-GPU
latency trace. For comparison work, use the same device list and arguments on
both runs.
