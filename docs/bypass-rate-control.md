# Automatic low-crack-rate bypass

Shooter can stop spending time on a dictionary or mask after its crack rate
falls below a useful minimum. Both options are required:

- `--bypass-delay=N` checks the result every `N` seconds.
- `--bypass-threshold=N` requires at least `N` new cracks during each window.

If a window produces fewer new cracks than the threshold, Hashcat bypasses the
current queue entry and starts the next dictionary or mask. If there is no next
entry, the job ends with `Status: Bypass`.

## Example

This example checks every five minutes. The current dictionary continues only
when it finds at least ten new hashes during that five-minute window:

```powershell
.\hashcat.exe -m 0 -a 0 hashes.txt words-strong.txt words-broad.txt `
  --bypass-delay=300 --bypass-threshold=10
```

For a mask queue, use the same paired options with the masks or mask file you
normally run:

```powershell
.\hashcat.exe -m 0 -a 3 hashes.txt masks.hcmask `
  --bypass-delay=300 --bypass-threshold=10
```

The console reports the transition:

```text
Bypass threshold reached! Next dictionary / mask in queue selected. Bypassing current one.
```

## How the count works

- Only hashes newly cracked by the current process count toward the threshold.
  Hashes already present in the potfile do not make an unproductive attack
  appear successful.
- After a successful window, the new-crack baseline resets and another window
  begins.
- Time spent paused is excluded from the delay window.
- The two options must be supplied together. Supplying only one is an error.

Choose a delay long enough to smooth out short bursts. Very small windows can
skip an otherwise useful attack simply because results arrive unevenly. Start
with a conservative threshold, observe several jobs, and adjust it for the
size and quality of your target list.
