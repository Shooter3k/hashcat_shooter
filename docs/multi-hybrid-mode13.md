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
that result. Rule files are executed on the host so intermediate placement is
preserved.

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

Rules are part of the ordered base pipeline, not a final GPU amplifier, so
skip, limit, checkpoint, and restore positions cover the complete product
above. Restore files preserve the original command-line order.

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

## Limits and performance

- Mode 13 requires normal GPU execution. <code>-S/--slow-candidates</code>
  and brain client mode are not supported.
- <code>-i/--increment</code> is not supported for mode-13 mask stages. Use an
  hcmask file when several mask lengths are needed.
- Wordlists must be readable regular files; directories are not accepted.
- Every intermediate candidate must fit the 256-byte host rule buffer. The
  final candidate must also fit the selected hash mode and kernel limit.
- The product of all stage sizes must fit unsigned 64-bit keyspace accounting.
  Overflow is detected and rejected.
- Intermediate rule stages run on the CPU to preserve exact placement. They
  are more flexible, but generally slower, than a conventional final GPU rule
  amplifier.

Attack mode 12 is unchanged: it remains the one-wordlist <code>?w</code>
hybrid with an optional second <code>?q</code> wordlist.
