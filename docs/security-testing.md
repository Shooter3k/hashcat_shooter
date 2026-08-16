# Security and parser testing

Shooter's `Security` GitHub Actions workflow builds the shared parser core
with Clang AddressSanitizer and UndefinedBehaviorSanitizer, then runs a
coverage-guided libFuzzer target over the common input tokenizer and the
multibyte rule compiler. A starter corpus covers token boundaries and UTF-8
rule literals. Crashes, timeouts, and out-of-memory test cases are retained as
workflow artifacts.

The workflow runs for pushes and pull requests that change parser, rule,
fuzzer, or build sources, on a weekly schedule, and on manual request. It is a
CPU-only check and does not require a GPU.

On a Linux host with Clang and libFuzzer support, build or run it directly:

```bash
make clean
make DEBUG=2 CC=clang CXX=clang++ fuzz-parser
make DEBUG=2 CC=clang CXX=clang++ fuzz-parser-run
```

`DEBUG=2` enables both address and undefined-behavior sanitizers throughout
the common core. The fuzzer's default local run is 60 seconds; CI uses a
longer bounded run and uploads any reproducer.

The clean step prevents a normal production archive from being reused in a
sanitized build. Rebuild normally after fuzzing before packaging a release.
