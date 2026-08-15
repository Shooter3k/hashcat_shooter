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

- 264 mdxfind modes use an isolated wrapper around a compatible existing
  hashcat module. These keep that module's parser, self-test, optimized kernel,
  and input format.
- 737 modes use the bundled mdxfind compatibility bridge. Its native CPU
  units use the Hashpipe verifier implementations exercised by mdxfind's
  published test vectors, with mdxfind's checked-in expression VM as a
  fallback. Hashcat's comparison kernel performs normal multi-hash lookup on
  verified results.

The documented algorithm set is verified against all 988 vectors in
Hashpipe's `HASH_TYPES.md`; direct known-answer tests cover the eleven
standalone modes omitted from that document. `e426` (`PARALLEL`) is a
scheduler pseudo-entry, not a hash algorithm, and `e535`
(`SHA1-CUSTOMUSERSALT`) is an mdxfind outlier that depends on its external
custom-user/salt state and has no published standalone vector. Their named
entries remain present so the registry and CLI stay complete; neither is
silently redirected to a different algorithm.

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

`e987` additionally accepts the Magento Argon2id forms supported by mdxfind:

```text
hex_digest:salt:2
hex_digest:salt:3_digest_length_iterations_memory_bytes
```

These are translated internally to hashcat's Argon2 representation using the
same mdxfind rules: version 19, one lane, and the first 16 salt characters.
The original Magento line is retained for potfile, `--show`, `--left`, and
outfile output.

## Regeneration

Run the generator whenever mdxfind's live registry changes:

```powershell
python tools/generate_mdxfind_modules.py `
  --mdxfind C:\path\to\mdxfind-main `
  --hashcat .
```

The generator reads `Types[]` and `Maphashcat[]` from `mdxfind.c`, validates
that the expected 1,001 entries are present, writes every `module_eN.c`
wrapper, and refreshes the JSON inventory. The bridge VM sources are vendored
from mdxfind under `src/bridges/mdxfind`. The Hashpipe verifier and the source
dependencies it needs are under `src/bridges/mdxfind/hashpipe`, together with
their license notices.
