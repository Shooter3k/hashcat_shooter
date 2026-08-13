# mdxfind named modules

This build exposes every algorithm in mdxfind's live registry as a named
hashcat module. The public names are `e1` through `e1001`, exactly matching the
identifiers accepted by mdxfind.

```powershell
hashcat.exe -m e987 hashes.txt wordlist.txt
```

The same public spelling is used by help, hash information, status,
benchmark, autodetect, diagnostic, and machine-readable output. For example,
`hashcat.exe -hh` lists `e987`, not its private integer representation.

The `eN` spelling is translated to a reserved internal range only after
command-line parsing. It is then loaded from `modules/module_eN.dll` on Windows
or `modules/module_eN.so` on Unix-like systems. Ordinary numeric modes still
use the original `module_00000` filename convention.

## Implementations

The generated modules never edit or replace an existing hashcat module.

- 302 mdxfind modes use an isolated wrapper around a compatible existing
  hashcat module. These keep that module's parser, self-test, optimized kernel,
  and input format.
- 699 modes use the bundled mdxfind expression bridge. Candidate expressions
  are evaluated on native CPU bridge units, and hashcat's comparison kernel
  performs normal multi-hash lookup on the results.

mdxfind's checked-in expression table does not contain executable bytecode for
every registry entry. Some entries are explicitly classified as outliers, and
some expression programs require legacy primitives not linked into this
bridge. Their named modules still exist so the registry and CLI are complete,
but they stop at bridge initialization with a specific diagnostic. They never
fall back to a different hash algorithm.

The complete generated inventory is in
[`mdxfind-modules.json`](mdxfind-modules.json). An `implementation` value that
starts with `hashcat-` identifies the reused numeric mode; `mdxfind-bridge`
identifies an expression front end.

## Expression input fields

An unsalted expression takes one value per hash-list line:

```text
hash
```

Expression modules can receive up to four mdxfind fields after the target:

```text
hash:salt:salt2:pepper:user
```

Trailing fields may be omitted. A mapped hashcat wrapper instead uses the
original numeric module's documented syntax, visible with:

```powershell
hashcat.exe -m e987 --example-hashes
```

## Regeneration

Run the generator whenever mdxfind's live registry changes:

```powershell
python tools/generate_mdxfind_modules.py `
  --mdxfind M:\junk\mdxfind-main `
  --hashcat M:\github\hashcat_shooter
```

The generator reads `Types[]` and `Maphashcat[]` from `mdxfind.c`, validates
that the expected 1,001 algorithms are present, writes every `module_eN.c`
wrapper, and refreshes the JSON inventory. The bridge VM sources are vendored
from mdxfind under `src/bridges/mdxfind` with their MIT license.
