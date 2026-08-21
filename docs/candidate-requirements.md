# Final-candidate requirements

Shooter can reject candidates that do not contain required character classes.
This is a global candidate policy, separate from PCFG and separate from any
rule file.

The convenient one-or-more flags are:

```text
--require-upper
--require-lower
--require-digit
--require-symbol
```

For example, require at least one byte from every class:

```powershell
.\hashcat.exe -m 0 -a 0 hashes.txt words.txt `
  --require-upper --require-lower --require-digit --require-symbol
```

All four options are off by default. Omitting them preserves normal Hashcat
candidate generation and performance.

## Requiring more than one

The numeric forms set an exact minimum:

```text
--candidate-min-upper=N
--candidate-min-lower=N
--candidate-min-digit=N
--candidate-min-symbol=N
```

For example:

```powershell
.\hashcat.exe -m 0 -a 0 hashes.txt words.txt `
  --candidate-min-upper=2 `
  --candidate-min-lower=4 `
  --candidate-min-digit=2 `
  --candidate-min-symbol=1
```

The `--require-*` form is equivalent to a minimum of one. If a class is named
more than once, the largest requested minimum wins regardless of option order.

Each minimum and the sum of all four minimums must fit Hashcat's 256-byte
candidate limit. An impossible total is rejected at startup instead of
silently running an empty attack.

## Exact class definitions

Requirements classify candidate bytes, not locale-dependent characters:

| Requirement | Accepted bytes |
| --- | --- |
| upper | ASCII `A` through `Z` |
| lower | ASCII `a` through `z` |
| digit | ASCII `0` through `9` |
| symbol | every byte not in the three classes above |

`--require-symbol` is the “at least one special character” check. Spaces,
punctuation, control bytes, and non-ASCII bytes all count as symbols. A
multi-byte UTF-8 character contributes one symbol count for each encoded byte,
because Hashcat's candidate interface and these options operate on bytes.

The four classes are mutually exclusive, so one byte cannot satisfy two
minimums.

## When the check runs

The policy is evaluated against the final candidate that would be hashed, not
the original wordlist line.

- In attack mode 0, wordlist decoding, conversion, inline transformation,
  and ordinary `-r` rules run first.
- In attack mode 8, the feed emits its candidate and ordinary rules run
  first.
- In attack modes 1, 3, 6, 7, and 12 without whole-candidate rule
  amplification, the complete wordlist/mask combination is assembled first.
- In attack mode 13, every left-to-right wordlist, mask, and rule stage runs
  first; the completed pipeline candidate is then checked.

This ordering matters. Given the wordlist entry `password1!` and rule `c`,
the final candidate is `Password1!`. It satisfies all four one-or-more flags
even though the original word had no uppercase byte.

PCFG does not learn or embed these requirements. The following command first
generates and rules PCFG candidates, then applies the independent policy:

```powershell
.\hashcat.exe -m 0 -a 8 hashes.txt `
  -r rules\best64.rule `
  --require-upper --require-lower --require-digit --require-symbol `
  pcfg models\organization.pcfg
```

For an ordered mode-13 pipeline:

```powershell
.\hashcat.exe -m 0 -a 13 hashes.txt `
  words.txt "mask:?d" `
  -r rules\capitalize.rule `
  "mask:!" `
  --require-upper --require-lower --require-digit --require-symbol
```

The final literal mask is written as `mask:!` because a literal-only mode-13
mask would otherwise look like a filename.

## Progress, rejects, skip, and restore

A failed requirement rejects the candidate in place. It does not remove or
renumber the candidate's original position. Consequently:

- `--skip` and `--limit` retain their normal positional meaning;
- restore files keep the same candidate mapping;
- multi-GPU devices continue to receive nonoverlapping ranges;
- the status `Rejected` line includes policy failures;
- progress reaches the original unfiltered keyspace.

`--keyspace` therefore reports the structural keyspace before policy
rejection. Computing the number that will pass would require generating and
inspecting every candidate, defeating the purpose of a quick keyspace query.

When active, a normal status page identifies the policy explicitly:

```text
Candidate.Policy.: upper>=1, lower>=1, digit>=1, symbol>=1 (final candidate)
```

## Performance

For most classic attacks, an exact final-candidate check selects Hashcat's
host-generator path automatically. The host assembles candidates, applies
rules where supported, checks the byte counts, and transfers only accepted
candidates. This guarantees correct post-rule semantics, but it can reduce
throughput on fast hashes because CPU generation and PCIe transfer can become
the bottleneck.

Mode 13 already assembles its ordered pipeline on the host. When a policy is
active, its trailing GPU amplifier is disabled so no unexamined suffix can be
added after the check. This also trades some speed for exact final-candidate
semantics.

These options are most useful when the policy removes a large amount of
low-value work or when the selected hash is slow enough that candidate
generation is not the limiting stage. Measure both recovered hashes per unit
of time and total throughput; a high reject percentage is not automatically
a net performance improvement.

The status `Candidate.Engine.: Host Generator + PCIe` confirms that the
host path is active.

## Compatibility limits

The first implementation deliberately refuses combinations for which it
cannot inspect the true final candidate:

- standard-input mode and classic `--stdout` slow-candidate output are not
  compatible;
- mode 13 has its own exact host output path, so filtered mode-13 `--stdout`
  is supported;
- mode 1 with more than two wordlists is not compatible with the automatic
  slow-candidate path; express that job in mode 13 when requirements are
  needed;
- Shooter's added whole-candidate `-r`/`-g` paths in modes 1, 3, 6, and 7
  use GPU rule amplification and cannot be combined with these host-side
  requirements;
- use mode 13 when a mask/wordlist combination needs both ordered rule stages
  and a final policy;
- association mode 9 is not part of the candidate-policy path;
- normal restrictions for `--slow-candidates`, brain client, benchmarks, and
  the selected hash mode still apply.

Mode 0 and mode 8 rules are host-applied under the policy and are supported.
Mode 13 rule stages are also supported because its final pipeline candidate
exists on the host before the check.

Refusing an unsupported layout is important. Checking a candidate before a
later GPU rule or suffix would make the option appear to work while hashing
candidates that do not meet the stated policy.

## Choosing a candidate strategy

Requirements are filters, not generators. They cannot add a missing uppercase
letter, digit, or symbol. A rule or generator must first create candidates
that can pass.

For a wordlist, use a compact rule set that creates the desired variations,
then filter the ruled result. For PCFG, train the grammar on representative
authorized data and add requirements only if the audit policy warrants them.
For mixed wordlists, masks, and rule stages, mode 13 provides the clearest
left-to-right semantics.

Avoid treating common composition policies as proof that a candidate is
strong. A password can contain all four classes and still be predictable.
These options are a way to focus an authorized audit, not a password-strength
estimator.
