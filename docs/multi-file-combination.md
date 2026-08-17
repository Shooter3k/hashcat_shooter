# Multi-file combination with attack mode 1

Shooter attack mode 1 accepts two or more wordlists and concatenates one entry
from each file in command-line order:

```text
candidate = word1 + word2 + ... + wordN
```

The former private Shooter attack modes 11, 12, 13, and 14 are removed. Their
3-, 4-, 5-, and 6-file attacks are now ordinary mode-1 commands, and mode 1 is
not limited to six files. Official hashcat attack mode 12 is now present too,
but it is a separate general multi-hybrid mode using `?w` and optional `?q` in
a mask; it does not replace this multi-file mode-1 extension. Shooter now
reuses number 13 for a different ordered multi-hybrid syntax documented in
[multi-hybrid-mode13.md](multi-hybrid-mode13.md); the old fixed three-file
mode-13 syntax remains removed.

## Windows examples

```powershell
# Standard two-file combinator (native optimized path)
.\hashcat.exe -m 0 -a 1 hashes.txt words1.txt words2.txt

# Three, six, or eight files
.\hashcat.exe -m 0 -a 1 hashes.txt words1.txt words2.txt words3.txt
.\hashcat.exe -m 0 -a 1 hashes.txt words1.txt words2.txt words3.txt words4.txt words5.txt words6.txt
.\hashcat.exe -m 0 -a 1 hashes.txt words1.txt words2.txt words3.txt words4.txt words5.txt words6.txt words7.txt words8.txt

# Apply a rule after all files have been concatenated
.\hashcat.exe -m 0 -a 1 hashes.txt words1.txt words2.txt words3.txt -r rules\best66.rule
```

Two-file attacks without `-r` or `-g` retain hashcat's native optimized
combinator path, including its choice of the larger wordlist as the base. For
three or more files without whole-candidate rules, Shooter concatenates the
first `N - 1` words in the pipelined host producer and uses the final wordlist
as the GPU combinator amplifier.

Each GPU seeks directly to the mixed-radix wordlist position for its assigned
base range. On a multi-GPU system, a later device does not generate and throw
away the ranges assigned to earlier devices.

With `-r` or `-g`, Shooter assembles all `N` words on the host and sends the
complete base candidate through the GPU straight-rule amplifier. Standard
side rules execute first: `-j` applies to the first wordlist, `-k` applies to
every remaining wordlist, and `-r`/`-g` then applies to the concatenated value.

## Counting and restore units

Without whole-candidate rules and with three or more files:

```text
--keyspace          = count(file1) * ... * count(fileN-1)
--total-candidates  = count(file1) * ... * count(fileN)
```

This follows normal hashcat terminology: keyspace and restore positions count
base work units, while total candidates includes the final GPU amplifier.
With `-r` or `-g`, all files form the base and the rules are the amplifier:

```text
--keyspace          = count(file1) * ... * count(fileN)
--total-candidates  = --keyspace * loaded_rules
```

`--stdout`, `--skip`, `--limit`, checkpoints, `.restore`, status, potfiles,
and outfiles use these same normal base-work and amplifier units.

## Limits

- Three-or-more-file mode 1 requires normal GPU execution; `-S` and brain
  client operation are rejected. The unchanged two-file no-rule path retains
  its existing capabilities.
- Candidate length cannot exceed the selected hash mode and kernel limit.
- The product of all wordlist counts must fit in hashcat's unsigned 64-bit
  keyspace accounting. Overflow is detected and rejected.
- The practical number of files is also bounded by the Windows command-line
  length and by candidate length. The implementation has no fixed six-file
  array or attack-mode ceiling.

See [whole-candidate-rules.md](whole-candidate-rules.md) for rule behavior on
the other supported attacks.
