# Ordered component pipeline: attack mode 13

Shooter attack mode 13 accepts any number of wordlists, masks, and rule stages
in any order. Components are processed from left to right exactly where they
appear on the command line:

~~~text
hashcat -m HASH_MODE -a 13 HASH COMPONENT [COMPONENT ...]
~~~

For <code>--stdout</code>, <code>--keyspace</code>, or
<code>--total-candidates</code>, omit <code>HASH</code> as usual.

## Component syntax

Mode 13 recognizes these stages:

| Command-line component | Stage |
| --- | --- |
| A normal existing file | Wordlist |
| A token containing <code>?</code>, such as <code>?d?d</code> | Inline mask |
| A file ending in <code>.hcmask</code> | Mask-file stage |
| <code>-r RULEFILE</code> | Rule-file stage at that exact command-line position |
| <code>-g COUNT</code> | Generated-rule stage at that exact command-line position |

The explicit forms below resolve ambiguous names:

| Prefix | Meaning |
| --- | --- |
| <code>wordlist:PATH</code> | Force a file, including a <code>.hcmask</code> file, to be a wordlist |
| <code>mask:MASK</code> | Force an inline mask; use this for a literal-only mask with no <code>?</code> |
| <code>maskfile:PATH</code> | Force a file with any extension to be an hcmask file |

Other hashcat options remain global and may appear normally. Only positional
components and the locations of <code>-r</code> and <code>-g</code> define
the mode-13 pipeline.

<code>?w</code> and <code>?q</code> are not mode-13 markers. They remain
attack-mode 12 syntax. In mode 13, place the wordlist itself at the position
where its candidate should be appended.

## Exact left-to-right behavior

A wordlist or mask stage appends its selected value to the candidate assembled
so far. A rule stage transforms that entire assembled prefix. Any later
wordlist or mask then appends to the transformed result.

For example:

~~~powershell
.\hashcat.exe -m 0 -a 13 hashes.txt first.txt "?d?d" `
  -r rules\append-bang.rule second.txt
~~~

For <code>alpha</code>, mask value <code>42</code>, rule <code>$!</code>,
and <code>omega</code>, the tested candidate is:

~~~text
alpha42!omega
~~~

The rule does not affect <code>omega</code>, because that wordlist is to the
rule's right. Move <code>-r rules\append-bang.rule</code> to the end to
produce <code>alpha42omega!</code>.

Each rule file is an independent Cartesian stage. Two adjacent
<code>-r</code> options apply sequentially: every rule from the first file
transforms the current prefix, then every rule from the second file transforms
that result. The implementation may compile a safe trailing group of stages
into GPU rules, but that does not change this stage-by-stage behavior.

These are all valid:

~~~text
wordlist wordlist mask -r rulefile wordlist
wordlist wordlist mask -r rulefile
mask wordlist wordlist -r rulefile
mask wordlist mask wordlist
mask -r rulefile mask wordlist mask -r another-rulefile
~~~

The command that originally exposed the old fixed-layout limitation now works
without changing its component order:

~~~powershell
.\hashcat.exe -m 0 -w 4 -a 13 hashes.txt `
  M:\emails\emails.txt.not4public.usernames.sorted_by_length `
  -r M:\rules\HashMob.1000.rule `
  "?d?d" `
  D:\unorganized\wordlists_already_run_by_shooter_rules\Domains100.dic
~~~

It builds:

~~~text
rule(username) + two digits + domain
~~~

## Ordering and keyspace

Stages use a deterministic mixed-radix order. The first command-line stage is
the outermost Cartesian position and the last stage is the innermost.
Wordlist entries retain file order. Hcmask lines retain file order, and each
line uses normal hashcat mask/Markov ordering.

For stage sizes <code>C1</code> through <code>CN</code>:

~~~text
--keyspace          = C1 * C2 * ... * CN
--total-candidates  = C1 * C2 * ... * CN
~~~

A wordlist stage size is its line count. An inline mask size is its mask
keyspace. An hcmask stage size is the sum of its non-comment mask-line
keyspaces. A rule stage size is its valid rule count.

Skip, limit, checkpoint, and restore positions cover the complete product
above, including every rule-stage choice. Restore files preserve the original
command-line order and the internal GPU split used by the saved session.

## Interactive controls

Normal terminal attacks display Hashcat's interactive command menu and accept
the usual one-key controls without Enter. In particular, `s` prints a live
status page. Pause, bypass, checkpoint, finish, quit, runtime adjustment, and
outfile-ignore controls appear and operate when their corresponding options
and runtime state make them available. Mode 13 also prints the automatic full
status page once before cracking starts and once after the attack completes.

`--stdout` is intentionally noninteractive: it emits candidates only and does
not insert the command menu or automatic status pages into pipeline output.

`Time.Estimated` uses the complete ordered-pipeline progress and aggregate
device speed. The automatic pre-attack page is printed before a speed sample
exists and can therefore show `0 secs` beside `0 H/s`. Periodic pages and `s`
show the measured ETA as soon as cracking has produced a speed sample.

## Wordlist side rules and masks

Side rules retain their established meaning: <code>-j</code> transforms
entries from the first wordlist stage, while <code>-k</code> transforms
entries from every later wordlist stage. These transformations occur as each
wordlist is read. Ordered <code>-r</code> and <code>-g</code> stages then
transform the prefix at their own positions.

Normal inline masks and hcmask per-line custom charsets are supported.
Multiple mask stages are allowed in one command. For example,
<code>?u?l words.txt ?d?d</code> appends a two-character mask, a word, and a
two-digit mask.

## GPU batching and performance

Mode 13 automatically searches for the largest safe trailing group of stages
whose Cartesian product is at most 65,536 candidates. It compiles those
wordlist values, masks, and supported rules into native kernel rules. The host
then sends one assembled prefix while each GPU evaluates the entire compiled
suffix. Status makes the split visible:

~~~text
Guess.GPU.Amp....: 10000 candidates per host prefix (stages #02-#04)
~~~

This is especially important for very fast hashes. On the reference system
with twelve RTX 4090 GPUs and one MD5 target, suffixes of 100 candidates were
not enough to keep the devices busy, 1,000 candidates were a substantial but
uneven improvement, and 10,000 candidates held all twelve devices at roughly
98 to 100 percent utilization. The pipeline from the earlier example has
exactly that useful 10,000-candidate suffix:

~~~text
large-wordlist -r ten-rules ?d one-hundred-domains
               10 rules * 10 digits * 100 domains = 10,000
~~~

The first stage remains the outer host wordlist, while the rule, digit, and
domain choices execute as the GPU suffix. Rules can occur anywhere inside a
compiled suffix; their left-to-right scope is unchanged.

This is automatic, not a promise that every possible pipeline can reach 100
percent utilization. A single wordlist has a suffix multiplier of one, for
example, and there is no correct way to invent additional candidates merely
to make a utilization meter read higher. For best throughput, keep a large
outer source to the left and, when it matches the intended candidate semantics,
place enough small masks, rule files, or small wordlists to its right to create
about 10,000 or more suffix combinations. Do not reorder stages solely for
speed when doing so would change the desired candidates.

The compiled suffix is limited to 65,536 composite rules and 31 kernel-rule
commands per composite. A suffix wordlist entry must fit that command limit.
If a stage uses an unsupported rule operation, an overlong literal, or a
larger product, mode 13 tries a shorter suffix and leaves the other stages on
the exact host path. <code>--stdout</code> always uses the host path to retain
its exact emission order. A non-divisible <code>--skip</code> or
<code>--limit</code> boundary also falls back instead of rounding the requested
window. Restore files created before the GPU split use the compatible host
mapping.

Large first host-side wordlists use Shooter's sparse indexed feed, even when
a mask or rule precedes that wordlist. This prevents different GPU dispatchers
from repeatedly rescanning billions of earlier lines to reach their assigned
ranges.

The utilization figures above measure candidate computation against one
digest. A target containing tens of millions of digests has additional bitmap
and comparison work, so its displayed cracking rate is not directly
comparable even when GPU utilization is equally high.

An exhaustive four-stage benchmark tested all 24 permutations of the same
4,461,259,249-line wordlist, 10-rule file, `?d` mask, and 100-line domain
wordlist against one MD5 target. All six orders with the large wordlist first
formed a 10,000-candidate suffix, held every RTX 4090 at 99 to 100 percent, and
measured 206.4 to 219.5 GH/s. The original order—large wordlist, rules, mask,
then domain wordlist—was the fastest run at 219.5 GH/s. The other 18 orders
formed multipliers of 1, 10, 100, or 1,000 and did not keep the complete fleet
busy. This verifies the automatic split and the ordering guidance; it does not
justify reordering stages when that would change the intended candidates.

The same best order was then measured against the actual 84,381,739-digest
MD5 target with `--bitmap-max 26`. Five consecutive NVIDIA samples put every
GPU at 98 to 100 percent, per-device rates stayed balanced, and aggregate
speed was 100.8 to 101.3 GH/s. The large target is comparison-bound and is not
expected to match the one-digest rate. Building its 26-bit bitmap took about
17 seconds, but the larger bitmap materially improved attack throughput for
this target.

## Reproducible validation

`tools/test_mode13_exhaustive.py` enumerates every wordlist/mask/rule type
string from one through six stages. That is 1,092 structural pipelines and
55,986 ordered candidates per semantic path. It compares byte-for-byte output
with an independent left-to-right oracle and can also validate a development
build's compiled suffix rules.

A separate 60-stage repeated wordlist/mask/rule stress case verifies that the
runtime pipeline is not capped at the six-stage exhaustive-test bound.

`tools/benchmark_mode13_orders.py` runs all 24 permutations of a supplied
large wordlist, rule file, mask, and small suffix wordlist. It records the GPU
amplifier, aggregate speed, and per-GPU NVIDIA utilization. The benchmark
refuses to start when a card is already busy so a background job cannot make
the result look better or worse than it is.

## Additional limits

The host generator caches each assembled prefix until one of its stages
changes. In a pipeline such as <code>wordlist -r rules ?d domains.txt</code>,
the rule result is therefore computed once for all of its digit/domain
suffixes rather than once per final candidate. Mixed-radix positions advance
as an odometer, small wordlists share an 8 MiB resident-cache budget per
device, and forward jumps continue from the current large-wordlist cursor.
The emitted candidates and their left-to-right order remain identical.

- Mode 13 requires normal GPU execution. <code>-S/--slow-candidates</code>
  and brain client mode are not supported.
- <code>-i/--increment</code> is not supported for mode-13 mask stages. Use an
  hcmask file when several mask lengths are needed.
- Wordlists must be readable regular files; directories are not accepted.
- Every intermediate candidate must fit the 256-byte host rule buffer. The
  final candidate must also fit the selected hash mode and kernel limit.
- The product of all stage sizes must fit unsigned 64-bit keyspace accounting.
  Overflow is detected and rejected.
- Stages that cannot be represented by the bounded GPU suffix run on the host.
  Exact left-to-right behavior is retained in either path.

Attack mode 12 is unchanged: it remains the one-wordlist <code>?w</code>
hybrid with an optional second <code>?q</code> wordlist.
