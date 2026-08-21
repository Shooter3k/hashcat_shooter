# Candidate-generation methods: implementation roadmap

This document keeps the six proposed high-value candidate methods separate
from the shipped-feature inventory. PCFG is implemented now. Methods 2 through
6 are design targets and must not be read as available command-line features.

Each method is intended for passwords and systems the operator owns or is
explicitly authorized to audit.

## Status summary

| # | Method | Status | Intended integration |
| ---: | --- | --- | --- |
| 1 | Probabilistic context-free grammar (PCFG) | Shipped | `-a 8 pcfg MODEL` |
| 2 | Adaptive recovery scheduler | Planned | orchestration layer over existing attacks |
| 3 | Native PRINCE generator | Planned | deterministic attack-mode 8 feed |
| 4 | Target-aware association candidates | Planned | association feed with explicit target data |
| 5 | Keyboard-walk generator | Planned | deterministic attack-mode 8 feed |
| 6 | Passphrase grammar | Planned | trained attack-mode 8 feed |

## 1. Probabilistic context-free grammar

PCFG learns class/length structures and terminal distributions from an
authorized plaintext corpus. It combines the learned pieces and emits the
lowest negative-log probability candidates first.

Why it is useful:

- concentrates early work on observed password shapes;
- creates combinations not present verbatim in the training corpus;
- provides deterministic keyspace, skip, restore, and multi-GPU positions;
- can use ordinary Hashcat rule amplification;
- remains independent of optional final-candidate requirements.

The shipped implementation consists of `tools/train_pcfg.py` and the native
`pcfg` feed for attack mode 8. See [Native PCFG candidate generation](pcfg-attack.md).

## 2. Adaptive recovery scheduler

An adaptive scheduler would run several existing attacks in bounded slices,
measure which slices recover new hashes, and allocate later time to the most
productive families. The generator itself need not inspect hashes or learn
plaintext online; it can coordinate already-authorized wordlist, mask, rule,
PCFG, PRINCE, and passphrase jobs.

The important metric is marginal recovery, not raw H/s:

```text
newly recovered digests / elapsed slice time
```

A sound implementation needs:

- reproducible job definitions and candidate-range boundaries;
- a minimum exploration budget so one noisy early slice does not permanently
  starve another method;
- diminishing-return detection based on new recoveries;
- checkpointed scheduler state and resumable child sessions;
- deduplication across methods without changing their own restore mappings;
- explicit limits on total time, GPU allocation, and target scope;
- reporting that distinguishes measured recovery from scheduling overhead.

This belongs above Hashcat's attack modes rather than inside a new numeric
mode. It changes which attack runs next, not the candidate semantics of a
single attack.

## 3. Native PRINCE generator

PRINCE-style generation chains elements from a word corpus into candidates,
prioritizing plausible combinations and lengths. It is especially useful when
passwords concatenate names, words, dates, and short affixes without a fixed
two-word layout.

A native feed should provide:

- deterministic element ordering and chain enumeration;
- minimum and maximum element counts;
- minimum and maximum final byte lengths;
- optional per-element frequency weights;
- exact 64-bit keyspace when the selected bounds permit it;
- efficient direct seek or a documented replay strategy;
- independent per-device state;
- ordinary rule amplification after the complete chain;
- model/corpus identity for distributed sessions.

Attack mode 8 is the expected integration point. A feed avoids inventing a
new attack number and receives Hashcat's existing multi-device, status, rule,
and restore behavior.

## 4. Target-aware association candidates

Association generation derives candidates from data explicitly associated
with an authorized account or hash: username components, display name,
organization, domain, product, locale, dates, or other supplied attributes.
Useful transforms can combine normalized tokens, separators, years, numeric
suffixes, capitalization patterns, and compact rules.

The design must prevent accidental data mixing:

- every attribute record needs an unambiguous target key;
- a candidate for one target must never be tested against an unrelated target
  unless the operator opts into a global pass;
- input formats and normalization must be documented and deterministic;
- missing or duplicate target keys must fail visibly;
- status and output must retain the target association;
- private attributes must not appear in diagnostics by default.

Hashcat already has association-mode concepts. The planned work is a richer,
documented association source, not a claim that every target-aware strategy
requires another numbered attack mode.

## 5. Keyboard-walk generator

Keyboard walks model spatial movement over a named keyboard layout: adjacent
keys, direction changes, repeated steps, shifted variants, and short prefixes
or suffixes. They can cover candidates such as rows, diagonals, and turns that
are awkward to express with a compact mask.

A native generator should make the layout data explicit rather than assume one
physical keyboard. Required controls include:

- layout file and stable layout identifier;
- starting keys and permitted adjacency graph;
- minimum and maximum walk lengths;
- maximum direction changes and repeated keys;
- shift/case policy;
- optional numeric or symbol suffix stages;
- deterministic ordering, keyspace, seek, and restore;
- duplicate suppression when several paths spell the same byte sequence.

This is another natural attack-mode 8 feed. Rules can remain an independent
amplifier rather than being hard-coded into the walk generator.

## 6. Passphrase grammar

A passphrase grammar targets multiple natural-language words separated or
modified by spaces, punctuation, digits, and capitalization. It differs from
plain word chaining by learning or accepting phrase structures, word-role
distributions, separator distributions, and optional agreement constraints.

An initial implementation should support:

- a documented tokenized training format;
- language/model metadata and explicit text encoding;
- word-count and final-length bounds;
- probability-ordered phrase structures and terminal tables;
- separators and capitalization as modeled components;
- deterministic global ordering and 64-bit keyspace checks;
- corpus pruning controls and model confidentiality warnings;
- exact restore and multi-device behavior through attack mode 8;
- optional rules after the complete phrase.

It should not silently use an online language model or send training material
to an external service. A local, inspectable model keeps authorized corpus
handling and reproducibility under the operator's control.

## Shared acceptance criteria for methods 2-6

Before a planned method moves into the Shooter enhancement inventory, it
should satisfy the same baseline used by PCFG:

1. Candidate semantics and order are documented with small known-answer
   examples.
2. One candidate offset has the same meaning with one or many devices.
3. `--skip`, `--limit`, checkpoint, and restore are verified against a full
   uninterrupted sequence.
4. Keyspace is exact, explicitly unknown, or rejected on overflow; it is never
   silently wrapped.
5. Malformed inputs stop with actionable errors.
6. Rules and final-candidate requirements have explicit ordering semantics.
7. Fast-hash host/PCIe bottlenecks are measured rather than hidden.
8. Training models and target-associated inputs have privacy guidance.
9. Shipped documentation distinguishes implemented features from planned
   designs.

This staged approach lets each generator earn reliable restore and multi-GPU
behavior before another method is declared available.
