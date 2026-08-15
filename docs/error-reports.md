# Automatic error reports

No option is required. When `shooter_hashcat` reports its first error, it
creates a file like this in the directory where it was started:

```text
shooter_hashcat-error-20260815-153012-18420.log
```

The console prints the exact path once. All later errors and warnings from that
process go to the same file, so messages from several GPUs do not have to be
copied out of the console one at a time. The file also includes up to 64 recent
warnings that led up to the first error. This captures useful context such as a
`Hash parsing error` even though Hashcat classifies that message as a warning.
A successful run, or a run that prints only warnings, does not create a file.

## What to send with a bug report

1. Reproduce the problem once.
2. Find the path shown after `Error report saved to:`.
3. Open the file in a text editor and review it for private paths or arguments.
4. Attach the file to a
   [Shooter bug report](https://github.com/Shooter3k/shooter_hashcat/issues/new?template=bug_report.md)
   together with a short description of what happened and what you expected.

The report records:

- every normal error emitted during that process, with a timestamp;
- up to 64 recent warnings before the first error, plus all later warnings;
- the exact `shooter_hashcat` version;
- the operating system and CPU architecture;
- the process ID and working directory; and
- up to 256 command-line arguments, with each value limited to 4,096 bytes.

`--brain-password` values are always replaced with `[REDACTED]`. The report
does not attach hash lists, potfiles, wordlists, output files, or debug files.
Normal diagnostics can quote an individual malformed hash, rule, or other
input line, and command-line arguments can contain paths, masks, hashes, or
other private values. Always review the report before sharing it.

The report covers errors sent through Hashcat's normal error logger, including
startup, parsing, backend, GPU, kernel, runtime, and cleanup errors. A process
that is terminated by the operating system, loses power, crashes before the
logger is initialized, or cannot write to its starting directory may be unable
to create or finish the report.
