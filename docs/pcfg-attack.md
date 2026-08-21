# Native PCFG candidate generation

Shooter includes a deterministic probabilistic context-free grammar (PCFG)
candidate generator. It learns password shapes and the strings that appeared
inside each part of those shapes, then emits the most probable combinations
first.

PCFG is a feed for attack mode 8, not a new numbered attack mode:

```text
-a 8 pcfg MODEL
```

This separation is intentional. Attack mode 8 already provides the plugin
loading, per-device generator instances, rule amplification, keyspace,
`--skip`, `--limit`, restore, and multi-GPU distribution that a statistical
generator needs. `pcfg` is the generator selected inside that framework.

The PCFG feed is independent of Shooter's candidate-requirement options.
Training or running PCFG does not enable any class requirement. Requirements
can be added to a PCFG job, a wordlist job, a mask job, or mode 13 as a
separate policy; see [Final-candidate requirements](candidate-requirements.md).

Use only plaintext training data and target hashes that you own or are
explicitly authorized to audit. A PCFG model contains learned strings and can
reveal material from its training corpus. Treat the model as sensitive data.

## Quick start on Windows

Train a model from a plaintext corpus containing one password per line:

```powershell
python .\tools\train_pcfg.py `
  M:\training\authorized-passwords.txt `
  M:\models\organization.pcfg
```

Run the model against MD5 hashes:

```powershell
.\hashcat.exe -m 0 -a 8 M:\hashes\md5.txt `
  pcfg M:\models\organization.pcfg
```

Apply ordinary Hashcat rules to each complete PCFG candidate:

```powershell
.\hashcat.exe -m 0 -a 8 M:\hashes\md5.txt `
  -r M:\rules\best64.rule `
  pcfg M:\models\organization.pcfg
```

In this command PCFG produces a complete base candidate and `-r` transforms
that candidate. Rules are optional and are not part of the PCFG model.

Ask for the structural keyspace without starting an attack:

```powershell
.\hashcat.exe -a 8 --keyspace pcfg M:\models\organization.pcfg
```

Write candidates to a regular file:

```powershell
.\hashcat.exe -a 8 --stdout `
  pcfg M:\models\organization.pcfg `
  -o M:\candidates\organization.txt
```

Regular-file output is recommended for a resumable `--stdout` job. A pipe or
console can restore the candidate position, but it cannot retract data that a
downstream process already consumed.

## What the trainer learns

The trainer classifies every input byte into one of four classes:

| Token letter | Class | Bytes |
| --- | --- | --- |
| `U` | uppercase | ASCII `A` through `Z` |
| `L` | lowercase | ASCII `a` through `z` |
| `D` | digit | ASCII `0` through `9` |
| `S` | symbol/other | every other byte |

Consecutive bytes in the same class become one run. The run's token combines
its class and byte length. For example:

```text
Password2026!
U1,L7,D4,S1
```

The trainer records two distributions:

1. How often each complete structure occurs.
2. How often each terminal string occurs for a given token.

For the example above, `P` contributes to the `U1` terminal table,
`assword` to `L7`, `2026` to `D4`, and `!` to `S1`. A different training
password can contribute other values to the same tables. At generation time,
the grammar combines compatible terminals, which allows it to produce useful
unseen candidates rather than merely replaying its input.

Training is frequency-sensitive. Repeated corpus entries increase their
structure and terminal probabilities. Deduplicate the corpus if that is what
your experiment requires; retain duplicates if their observed frequency is
meaningful.

## Probability and candidate order

The trainer converts observed probabilities into integer negative base-2 log
scores. Lower scores are more probable. A candidate score is:

```text
structure score + score of terminal 1 + ... + score of terminal N
```

The native feed maintains a priority queue across every retained structure
and its terminal Cartesian product. It always emits the lowest total score
next. Ties are deterministic: structure order is compared first, then the
terminal indexes from left to right.

This is global best-first order. It does not finish one structure before
starting the next, and it does not approximate the merge with fixed-size
buckets. A high-probability candidate from any structure can precede a
lower-probability candidate from another structure.

The enumerator uses a canonical parent rule for a sorted multidimensional
sum. Every terminal-index tuple has exactly one parent, so the queue does not
emit duplicate tuples or need a global visited set. Different tuples can
still form the same byte string if a manually authored model contains
duplicate or overlapping grammar entries. Models produced by the supplied
trainer do not duplicate a terminal inside one token table.

## Trainer options

```text
python tools\train_pcfg.py INPUT OUTPUT [options]
```

| Option | Default | Meaning |
| --- | ---: | --- |
| `--max-length N` | `64` | Ignore training passwords longer than N bytes; maximum 256 |
| `--min-count N` | `1` | Keep only structures and terminals observed at least N times |
| `--max-structures N` | `50000` | Retain at most N structures, highest frequency first |
| `--max-terminals-per-token N` | `100000` | Retain at most N strings for each class/length token |

Examples:

```powershell
# A smaller exploratory model
python .\tools\train_pcfg.py corpus.txt small.pcfg `
  --min-count 2 `
  --max-structures 10000 `
  --max-terminals-per-token 25000

# Permit the complete Hashcat candidate length
python .\tools\train_pcfg.py corpus.txt long.pcfg --max-length 256
```

The trainer reports accepted passwords, ignored empty lines, ignored
over-length lines, structures written, terminals written, and the output
path. An empty corpus or limits that remove every structure are errors.

`--min-count` applies independently to structures and terminals. A retained
structure is removed if any table it needs becomes empty after terminal
filtering. Scores retain the probabilities measured from the original usable
corpus; pruning does not renormalize the retained subset.

## Model format

The model is a line-oriented, versioned format. Version 1 starts with:

```text
SHOOTER-PCFG<TAB>1
```

Blank lines and lines beginning with `#` are ignored. Terminal records must
appear before structures that reference them:

```text
T<TAB>TOKEN<TAB>SCORE<TAB>HEX_BYTES
S<TAB><TAB>SCORE<TAB>TOKEN[,TOKEN...]
```

Example:

```text
SHOOTER-PCFG	1
T	U1	0	50
T	L7	0	617373776f7264
T	D1	0	31
T	S1	0	21
S		0	U1,L7,D1,S1
```

This model emits `Password1!`. Hex encoding lets a terminal contain spaces,
tabs, zero bytes, or other data without making the record ambiguous.

At load time the feed validates:

- the exact format version;
- record and field structure;
- numeric score syntax;
- token class and byte length;
- hexadecimal terminal data;
- every terminal byte against its declared class;
- structure references and the 256-byte candidate limit;
- exact keyspace and score arithmetic against 64-bit overflow.

A malformed model stops before cracking. The feed also derives its source
identity from the complete normalized model contents, including all terminal
and structure scores. Two different grammars therefore cannot accidentally
share the same distributed-session identity merely because their file paths
match.

## Keyspace, skip, limit, and restore

For one structure, keyspace is the product of the sizes of all terminal
tables it references. Total model keyspace is the sum across structures:

```text
keyspace = sum(product(terminal counts in structure))
```

The feed computes this exactly and rejects a model whose product or sum does
not fit in Hashcat's 64-bit keyspace.

Candidate offsets refer to the deterministic global probability order.
`--skip`, `--limit`, checkpoints, and restore all use those offsets. Every
device creates the same ordered generator and Hashcat assigns nonoverlapping
ranges, so the candidate at a given offset is independent of device count and
device speed.

Version 1 seeking is deliberately simple and reliable: it resets the
priority queue and replays to an earlier or newly assigned offset. Forward
generation then continues normally. Deep restores or a far-away first range
can therefore spend noticeable CPU time replaying. The resulting candidate
is exact; this is a performance limitation, not an ordering limitation.

Changing the model between stopping and restoring changes the meaning of its
offsets. Keep the original model with the restore file.

## Rules and candidate requirements

PCFG supports ordinary `-r` and generated `-g` rule amplification because it
uses the attack-mode 8 rule interface. A rule transforms the complete PCFG
base candidate.

Candidate requirements are a different layer. This example requires one
uppercase letter, one lowercase letter, one digit, and one symbol after the
rule has run:

```powershell
.\hashcat.exe -m 0 -a 8 hashes.txt `
  -r rules\best64.rule `
  --require-upper --require-lower --require-digit --require-symbol `
  pcfg models\organization.pcfg
```

The PCFG model remains unchanged and can be reused without those options.
The requirements preserve original PCFG/rule positions; rejected candidates
appear in Hashcat's `Rejected` count. See the separate
[candidate-requirements guide](candidate-requirements.md) for performance and
compatibility details.

## Performance guidance

PCFG generation happens on the host and candidates cross PCIe. This is often
appropriate for slow hashes, where candidate quality matters more than raw
generator volume. For very fast hashes such as MD5, an unamplified PCFG feed
can become the bottleneck before the GPUs are full.

Useful approaches are:

- add a compact, high-value rule set so each transferred PCFG base candidate
  expands on the GPU;
- start with a pruned model and measure recovered hashes per unit of time;
- prefer observed, authorized training data relevant to the audited
  population;
- avoid enormous low-frequency terminal tables that consume queue memory but
  contribute little early value;
- use `--stage-profile` or `--task-time-breakdown` when diagnosing where a
  run spends time.

Rules change candidate order within each base candidate because Hashcat runs
the complete rule amplifier for that base. They also multiply total tested
candidates. `--keyspace` reports the PCFG base keyspace; use
`--total-candidates` when the complete amplified count is needed.

## Encoding and data handling

The trainer reads the corpus as bytes and removes only the final CR/LF line
ending. ASCII letters and digits receive their explicit classes. Every other
byte, including UTF-8 continuation bytes, spaces, punctuation, and control
bytes, is class `S`.

This byte model is deterministic and matches Hashcat's candidate interface,
but it is not Unicode-aware. A visible non-ASCII character encoded as UTF-8
can occupy several `S` bytes. Prepare the corpus in the encoding expected by
the selected hash mode, and do not mix encodings unintentionally.

The model contains terminal strings derived from the plaintext corpus. It is
not anonymized merely because those strings are hex encoded. Protect, retain,
and delete models according to the same authorization and data-handling rules
as the source corpus.

## Troubleshooting

`Usage: pcfg <model>` means the feed did not receive exactly one model path.
PCFG-specific switches belong to the trainer, not after the `pcfg` feed name.

`Structure references missing terminal table` means a model was edited or
truncated so an `S` record names a token without an earlier `T` record.

`Terminal ... contains a byte outside its declared class` means the token
label and decoded bytes disagree, for example an ASCII letter inside `D1`.

`PCFG keyspace exceeds 64 bits` means the retained Cartesian products cannot
be represented safely. Retrain with fewer structures, a higher minimum count,
or fewer terminals per token.

Low GPU utilization on a fast hash usually means host generation or PCIe
transfer is the limiting stage. Add a useful rule amplifier or use PCFG for a
slower hash rather than increasing model size blindly.

If a restore begins slowly at a deep offset, version 1 is replaying the
deterministic priority order. Let the seek finish, or start a new session with
a smaller/pruned model if that replay cost is unsuitable.
