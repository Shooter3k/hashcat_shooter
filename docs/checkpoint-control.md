# Multi-GPU checkpoint control

During a running attack, press `c` to request a checkpoint exit. Press `c`
again while the request is pending to cancel it.

In this shooter build, the request is coordinated across all active GPUs:

- Each GPU finishes its current safe unit of work and then waits at a device
  barrier without destroying its worker or backend context.
- Candidate producers pause with their consumers. Already-prefetched batches
  are retained, so cancelling does not skip reserved work.
- Cancelling releases every waiting GPU, including devices that reached their
  restore boundary before the other GPUs.
- If every live GPU reaches the barrier before cancellation, hashcat exits as
  `Aborted (Checkpoint)` and writes the normal session restore file.
- Skipped devices and devices that naturally finish at the end of the
  keyspace do not hold the barrier open.

The checkpoint boundary is still attack-dependent. A large mask amplifier or
another long inner sweep can continue running while `Checkpoint Quit
requested` is displayed; cancellation remains safe during that pending time.

To resume a completed checkpoint from PowerShell:

```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
.\hashcat.exe --session SESSION_NAME --restore
```
