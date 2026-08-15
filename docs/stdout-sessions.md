# Resumable `--stdout` sessions

Shooter build `.8` enables restore checkpoints and the normal interactive
controls for candidate-generation sessions.

## Recommended Windows usage

Use a regular outfile and a named session when you need exact continuation:

```powershell
M:\github\shooter_hashcat\hashcat.exe --stdout -a 3 "?d?d?d?d?d?d?d?d" `
  -o M:\work\candidates.txt `
  --session=stdout-candidates
```

The interactive menu is the same one used by a cracking session:

- `p` pauses candidate generation.
- `r` resumes a paused session.
- `c` requests a checkpoint and exits after every active GPU reaches a safe
  batch boundary.
- `q` stops immediately and leaves the most recent restore point available.
- `s`, `b`, and `f` retain their normal hashcat meanings.

Resume the saved command with:

```powershell
M:\github\shooter_hashcat\hashcat.exe `
  --session=stdout-candidates `
  --restore
```

Do not add the original mask, wordlist, rules, or outfile arguments to the
restore command. They are read from the `.restore` file.

## What an exact restore saves

For `--stdout -o` to a regular file, every checkpoint records two matching
values:

1. the end of the last fully committed candidate batch; and
2. the byte length of the outfile at that same point.

Candidate batches produced by multiple GPUs are committed in keyspace order.
If the process is stopped while a later or partial batch has already written
bytes, the next `--restore` truncates that uncommitted tail and then reopens the
file in append mode. This prevents a partial tail from being retained and
generated again.

For a nonzero saved boundary, the outfile must still exist and must be at least
that large. Hashcat refuses the restore instead of silently producing a damaged
file if the outfile is missing, shortened, locked, or not writable. A missing
file is allowed only when the saved byte boundary is zero.

## Output separation

Candidates remain on stdout when no `-o` is supplied. The menu, status text,
restore warnings, and other event messages are written to stderr. This keeps
stdout safe for redirection or a downstream process.

A console or pipe has no seekable byte boundary. Such a session can restore
its candidate position, but hashcat cannot retract data the downstream process
already accepted. Use a regular `-o` file when exact output continuation is
required.

## Stdin-fed candidate sources

When the candidate source itself is stdin, hashcat cannot also read menu keys
from stdin, so the interactive menu is unavailable. Restoring that kind of
session also requires the upstream producer to replay the same input. Prefer a
wordlist file or mask for unattended, resumable generation.

## Restore-file compatibility

This feature uses restore format version 721. Shooter can still read version
720 restore files for ordinary cracking sessions. A version-720 file does not
contain an stdout outfile byte boundary and therefore cannot be used for an
exact `--stdout -o` restore.
