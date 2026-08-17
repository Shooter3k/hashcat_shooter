# Ordered multi-hybrid attack mode 13

Shooter attack mode 13 combines any number of wordlists with a mask and then
optionally applies rules to the complete candidate. The command syntax is:

```text
hashcat -m HASH_MODE -a 13 HASH MASK_OR_HCMASK WORDLIST1 [WORDLIST2 ...] [-r RULE ...]
```

For `--stdout`, `--keyspace`, or `--total-candidates`, omit `HASH` as usual.

## Exact command-line ordering

Each `?w` marker maps to the next wordlist from left to right:

```text
mask:      PRE?wMID?wPOST?wEND
wordlists:    first  second third
candidate: PRE + first + MID + second + POST + third + END
```

Every mask must contain exactly one `?w` per wordlist. A mismatch is an error;
mode 13 never guesses, drops a wordlist, or reorders wordlists by size.
Consecutive markers are valid, so `?w?w?w` is an ordered three-wordlist
concatenation.

The iteration order is deterministic:

1. Masks from an hcmask file run in file order.
2. Wordlist tuples run in command-line order. The first wordlist is the
   outermost Cartesian position and the last wordlist is the innermost.
3. For each tuple, mask values run in hashcat's normal mask order. Default
   Markov ordering is preserved; use `--markov-disable` for literal charset
   order.
4. `-r` or `-g` rules run after the full candidate is assembled. Multiple
   `-r` files keep the order in which their options appeared.

For example:

```powershell
.\hashcat.exe -m 0 -a 13 hashes.txt "ID-?w-?d?d-?w" names.txt domains.txt -r rules\best66.rule
```

With `alice` from `names.txt`, `example` from `domains.txt`, mask digits `42`,
and rule `$!`, the tested candidate is:

```text
ID-alice-42-example!
```

## Multiple masks and rules

Pass an hcmask file as the mask argument to process any number of masks. Each
non-comment line is processed in file order and must contain the same number
of `?w` markers as the command has wordlists. Normal hcmask per-line custom
charsets are supported.

Repeat `-r` for multiple rule files:

```powershell
.\hashcat.exe -m 0 -a 13 hashes.txt layouts.hcmask one.txt two.txt three.txt `
  -r rules\first.rule -r rules\second.rule
```

Hashcat's normal rule-file combination semantics apply, with `first.rule`
before `second.rule`. Generated rules through `-g` are also applied to the
complete candidate.

Side rules retain their established meaning: `-j` transforms entries from the
first wordlist, while `-k` transforms entries from every remaining wordlist.
Those side transformations happen before marker substitution; `-r` and `-g`
happen afterward.

## Counting and restore units

For one mask whose generated mask keyspace is `M`, wordlist counts `W1` through
`WN`, and `R` loaded whole-candidate rules:

```text
--keyspace          = M * W1 * W2 * ... * WN
--total-candidates  = M * W1 * W2 * ... * WN * R
```

With no `-r` or `-g`, the straight kernel uses one no-op rule, so keyspace and
total candidates are equal. Multiple hcmask lines are separate ordered rounds;
whole-run reporting adds their candidate counts.

Skip, limit, checkpoint, and restore positions use the base keyspace before
whole-candidate rule amplification. Recovered plaintext, outfile, potfile, and
`--stdout` output contain the final ruled candidate.

## Limits

- Mode 13 requires normal GPU execution. `-S/--slow-candidates` and brain
  client mode are not supported.
- `-i/--increment` is not supported because every marker has a fixed
  wordlist mapping. Use multiple ordered hcmask lines for different layouts.
- Every wordlist must be a readable regular file. Wordlist directories are
  not accepted in this mode.
- The assembled base candidate must fit the selected hash mode and kernel
  length limit; normal rule processing enforces the final ruled limit.
- The product of mask and wordlist counts must fit unsigned 64-bit keyspace
  accounting. Overflow is detected and rejected.
- The practical number of wordlists is bounded by candidate length and the
  operating system's command-line length, not by a fixed wordlist array.

Attack mode 12 is unchanged. It remains the one-wordlist `?w` hybrid with an
optional second `?q` wordlist. Mode 13 deliberately uses only repeatable `?w`
markers so every wordlist's position is visible in the mask.
