# Whole-candidate rules for attack modes

The Shooter build can apply ordinary hashcat rules to the completed candidate
from attack modes 1, 3, 6, 7, and 8. No extra attack mode is needed.

| Attack mode | Base candidate before `-r`/`-g` |
| --- | --- |
| `-a 1` | `word1 + word2 + ... + wordN` (two or more wordlists) |
| `-a 3` | complete mask candidate |
| `-a 6` | `word + mask` |
| `-a 7` | `mask + word` |
| `-a 8` | complete candidate supplied by the feed |

Mode 8 already has native upstream rule support. Shooter adds the same final
whole-candidate rule behavior to the other modes in the table. Mode 9 keeps
its existing native association-rule support as well.

## Usage

Use `-r` for one or more rule files or `-g` for generated rules. For example:

```powershell
hashcat.exe -m 0 -a 1 hashes.txt left.txt right.txt -r rules\best66.rule
hashcat.exe -m 0 -a 1 hashes.txt one.txt two.txt three.txt four.txt -r rules\best66.rule
hashcat.exe -m 0 -a 3 hashes.txt "?u?l?l?l?d?d" -r rules\best66.rule
hashcat.exe -m 1000 -a 6 hashes.txt words.txt "?d?d?d?d" -r rules\best66.rule
hashcat.exe -m 1000 -a 7 hashes.txt "?d?d?d?d" words.txt -g 1000
```

For mode 1, words `pass`, `word`, and `2026`, followed by rule `$!`, test
`password2026!`. The rule runs after every word has been concatenated.

Mode-1 side rules keep their normal ordering:

```text
first     = -j(word1)
remaining = -k(word2) + ... + -k(wordN)
base      = first + remaining
tested candidate = each -r/-g rule applied to base
```

Rules are optional. With no `-r` or `-g`, each attack uses its original native
fast path and tests the unruled candidates exactly as before this feature.

## Counting, output, and restore

`--keyspace` remains the base keyspace of the selected attack.
`--total-candidates` includes rule amplification. For a ruled mode-1 attack,
if `C1` through `CF` are the wordlist counts and `R` is the number of loaded
rules:

```text
--keyspace          = C1 * C2 * ... * CF
--total-candidates  = C1 * C2 * ... * CF * R
```

`--skip`, `--limit`, checkpoints, and `.restore` positions continue to use the
base attack keyspace. Each base candidate is expanded through the full rule
set after restoration. Candidate display, recovered plaintext, outfile data,
and `--stdout` output reconstruct the final ruled candidate.

## Compatibility limits

- The added whole-candidate rule paths use normal GPU execution and do not
  support `--slow-candidates`.
- Brain-client operation is not supported for these added paths.
- Multi-file mode 1 is bounded by the Windows command-line length, the
  selected hash mode's candidate-length limit, and hashcat's 64-bit keyspace
  accounting. A product that overflows 64 bits is rejected explicitly.
- The normal hash-mode minimum and maximum candidate lengths are checked after
  the attack components are assembled and before the GPU rule is applied,
  matching straight-kernel base-word behavior.
- Rules that make the candidate exceed the selected kernel's supported length
  are rejected by the normal rule/kernel processing.

To migrate an older private mode 11-14 command, change the attack number to
`-a 1` and keep all of its wordlists in the same order. To migrate the
unreleased mode-15 syntax, replace `-a 15 left.txt right.txt -r rules.txt`
with `-a 1 left.txt right.txt -r rules.txt`.
